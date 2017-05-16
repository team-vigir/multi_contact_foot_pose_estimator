#include <pcl_conversions/pcl_conversions.h>

#include <pcl/io/pcd_io.h>
#include <pcl/kdtree/kdtree_flann.h>

#include <vigir_footstep_planning_lib/helper.h>
#include <vigir_footstep_planning_lib/math.h>

#include <vigir_terrain_classifier/terrain_model.h>

#include <multi_contact_point_estimator/uneven_terrain_stand/terrain_model_uneven.h>
#include <vigir_footstep_planning_msgs/Step.h>

#include <boost/archive/binary_oarchive.hpp>
#include "boost/archive/binary_iarchive.hpp"
#include "boost/archive/binary_oarchive.hpp"
#include "boost/iostreams/device/array.hpp"
#include "boost/iostreams/device/back_inserter.hpp"
#include "boost/iostreams/stream_buffer.hpp"
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <multi_contact_point_estimator/uneven_terrain_stand/foot/foot_state_uneven.h>
#include <multi_contact_point_estimator/uneven_terrain_stand/stand/uneven_terrain_stand.h>

using vigir_footstep_planning_msgs::Step;

//#include <multi_contact_point_estimator/uneven_terrain_stand/state_extended.h>


namespace vigir_footstep_planning
{
TerrainModelUneven::TerrainModelUneven(const std::string& name)
  : TerrainModelPlugin(name)
{

	// Read configuration parameters from the launch file
	ros::NodeHandle nh;
	std::string frozen_model_path;

	// Use the convex hull algorithm or the tensorflow prediction?
	if (ros::param::get("use_tensorflow_model", use_tensorflow_model)){
		ROS_INFO_STREAM("[MULTI_CP] Parameter use_tensorflow_model: " << use_tensorflow_model);
	} else {
		ROS_ERROR("[MULTI_CP] Parameter use_tensorflow_model could not get loaded in terrain_model_uneven, check the launch file.");
	}

	if(use_tensorflow_model) {
		if (ros::param::get("frozen_model_path", frozen_model_path)){
			ROS_INFO_STREAM("[MULTI_CP] Loading frozen model from: " << frozen_model_path);
			model = new MultiContactPointModel();
			model->init(frozen_model_path);
		} else {
			ROS_ERROR("[MULTI_CP] Parameter frozen_model_path could not get loaded in terrain_model_uneven, check the launch file.");
		}
	}

}

bool TerrainModelUneven::initialize(const vigir_generic_params::ParameterSet& params)
{
  if (!TerrainModelPlugin::initialize(params))
    return false;

  // get foot dimensions
  getFootSize(nh_, foot_size);

  // subscribe
  std::string topic;
  getParam("terrain_model_topic", topic, std::string("/terrain_model"));
  terrain_model_sub = nh_.subscribe(topic, 1, &TerrainModelUneven::setTerrainModel, this);

  return true;
}

bool TerrainModelUneven::loadParams(const vigir_generic_params::ParameterSet& params)
{
  if (!TerrainModelPlugin::loadParams(params))
    return false;

  params.getParam("foot_contact_support/min_sampling_steps_x", min_sampling_steps_x);
  params.getParam("foot_contact_support/min_sampling_steps_y", min_sampling_steps_y);
  params.getParam("foot_contact_support/max_sampling_steps_x", max_sampling_steps_x);
  params.getParam("foot_contact_support/max_sampling_steps_y", max_sampling_steps_y);
  params.getParam("foot_contact_support/max_intrusion_z", max_intrusion_z);
  params.getParam("foot_contact_support/max_ground_clearance", max_ground_clearance);
  params.getParam("foot_contact_support/minimal_support", minimal_support);

  return true;
}

void TerrainModelUneven::reset()
{
  TerrainModelPlugin::reset();

  boost::unique_lock<boost::shared_mutex> lock(terrain_model_shared_mutex);

  if (terrain_model)
    terrain_model->reset();
}

bool TerrainModelUneven::isAccessible(const State& s) const
{
  return s.getGroundContactSupport() >= minimal_support;
}



bool TerrainModelUneven::isAccessible(const State& next, const State& /*current*/) const
{
  return isAccessible(next);
}

bool TerrainModelUneven::isTerrainModelAvailable() const
{
  return terrain_model && terrain_model->hasTerrainModel();
}

void TerrainModelUneven::setTerrainModel(const vigir_terrain_classifier::TerrainModelMsg::ConstPtr& terrain_model)
{
  boost::unique_lock<boost::shared_mutex> lock(terrain_model_shared_mutex);

  // update terrain model
  if (!this->terrain_model)
    this->terrain_model.reset(new vigir_terrain_classifier::TerrainModel(*terrain_model));
  else
    this->terrain_model->fromMsg(*terrain_model);
}

double TerrainModelUneven::getResolution() const
{
  boost::shared_lock<boost::shared_mutex> lock(terrain_model_shared_mutex);
  return terrain_model->getResolution();
}

bool TerrainModelUneven::getPointWithNormal(const pcl::PointNormal& p_search, pcl::PointNormal& p_result) const
{
  boost::shared_lock<boost::shared_mutex> lock(terrain_model_shared_mutex);
  return terrain_model->getPointWithNormal(p_search, p_result);
}

bool TerrainModelUneven::getHeight(double x, double y, double& height) const
{
  boost::shared_lock<boost::shared_mutex> lock(terrain_model_shared_mutex);
  return terrain_model->getHeight(x, y, height);
}

bool TerrainModelUneven::getFootContactSupport(const geometry_msgs::Pose& p, double &support, pcl::PointCloud<pcl::PointXYZI>::Ptr checked_positions) const
{
  tf::Pose p_tf;
  tf::poseMsgToTF(p, p_tf);
  return getFootContactSupport(p_tf, support, checked_positions);
}

bool TerrainModelUneven::getFootContactSupport(const tf::Pose& p, double& support, pcl::PointCloud<pcl::PointXYZI>::Ptr checked_positions) const
{
  if (!getFootContactSupport(p, support, min_sampling_steps_x, min_sampling_steps_y, checked_positions))
    return false;

  // refinement of solution if needed
  if (support == 0.0) // collision, no refinement
  {
    return true;
  }
  else if (support < 0.95)
  {
    if (!getFootContactSupport(p, support, max_sampling_steps_x, max_sampling_steps_y, checked_positions))
      return false;
  }

  return true;
}

bool TerrainModelUneven::getFootContactSupport(const tf::Pose& p, double &support, unsigned int sampling_steps_x, unsigned int sampling_steps_y, pcl::PointCloud<pcl::PointXYZI>::Ptr checked_positions) const
{
  /// TODO: find efficient solution to prevent inconsistency
  //boost::shared_lock<boost::shared_mutex> lock(terrain_model_shared_mutex);

  support = 0.0;

  unsigned int contacts = 0;
  unsigned int unknown = 0;
  unsigned int total = 0;

  tf::Vector3 orig_pos;
  orig_pos.setZ(0.0);

  double foot_size_half_x = 0.5*foot_size.x;
  double foot_size_half_y = 0.5*foot_size.y;

  double sampling_step_x = foot_size.x/(double)(sampling_steps_x-1);
  double sampling_step_y = foot_size.y/(double)(sampling_steps_y-1);

  for (double y = -foot_size_half_y; y <= foot_size_half_y; y+=sampling_step_y)
  {
    orig_pos.setY(y);
    for (double x = -foot_size_half_x; x <= foot_size_half_x; x+=sampling_step_x)
    {
      total++;

      // determine point in world frame and get height at this point
      orig_pos.setX(x);

      const tf::Vector3 &trans_pos = p * orig_pos;

      double height = 0.0;
      if (!getHeight(trans_pos.getX(), trans_pos.getY(), height))
      {
        //ROS_WARN_THROTTLE(1.0, "getFootSupportArea: No height data found at %f/%f", p.getOrigin().getX(), p.getOrigin().getY());
        unknown++;
        continue;
      }

      // diff heights
      double diff = trans_pos.getZ()-height;

      // save evaluated point for visualization
      if (checked_positions)
      {
        pcl::PointXYZI p_checked;
        p_checked.x = trans_pos.getX();
        p_checked.y = trans_pos.getY();
        p_checked.z = trans_pos.getZ();
        p_checked.intensity = std::abs(diff);
        checked_positions->push_back(p_checked);

        //ROS_INFO("%f %f | %f %f | %f", x, y, p.z, height, diff);
      }

      // check diff in z
      if (diff < -max_intrusion_z) // collision -> no support!
        return true;
      else if (diff < max_ground_clearance) // ground contact
        contacts++;
    }
  }

  if (unknown == total)
  {
    return false;
  }
  else
  {
    /// @ TODO: refinement (center of pressure)
    support = static_cast<double>(contacts)/static_cast<double>(total);
    return true;
  }
}

bool TerrainModelUneven::update3DData(geometry_msgs::Pose& p) const
{

	return terrain_model->update3DData(p);
}


FootStateUneven TerrainModelUneven::getFootStateUneven(State& s) const{

	FootStateUneven footStand;
	FootForm footForm = FootForm(); // TODO put in constructor

	// calculate the foot stand (including the normal and support, contact points, point set, etc)
	try{

		UnevenTerrainStand unevenStand = UnevenTerrainStand(s, foot_size, terrain_model->getHeightGridMap(), footForm, model, use_tensorflow_model);
		footStand = unevenStand.getStand();

		if(footStand.getValid() != 1){
			return footStand;
		}

		std::vector<double> n = footStand.getNormal();
		s.setNormal(n[0], n[1], n[2]);
		s.setGroundContactSupport(footStand.getSupport());
		s.setZ(footStand.getHeight());

		// make sure that the pose does not contain NANs
		tf::Vector3 orig = s.getPose().getOrigin();
		for(int i = 0; i < 4; i++) {
		  if(std::isnan(orig.m_floats[i])) {
			  footStand.setValid(-1);
		  }
		}


	}catch(std::exception ex){
		// The above code calls the ML model, depending on the system this might use your GPU with CUDA.
		// If something goes wrong don't stop the whole planner, just try the next foot stand.

		footStand.setValid(-1);
		ROS_ERROR("[MULTI_CP] EXCEPTION: %s", ex.what());
	}

	return footStand;
}


bool TerrainModelUneven::update3DData(State& s) const
{
	FootStateUneven footStand = getFootStateUneven(s);
	return footStand.getValid() == 1;
}


bool TerrainModelUneven::update3DData(Step& step) const {
	State state(step);
	FootStateUneven footStand = getFootStateUneven(state);
	FootStateStruct footStepStruct = footStand.getFootStateStruct();

	FootStateStruct::serialize_step_data(footStepStruct, step);

	return footStand.getValid() == 1;
}

}// namespace

#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(vigir_footstep_planning::TerrainModelUneven,vigir_footstep_planning::TerrainModelPlugin)
