#ifndef VIGIR_FOOTSTEP_PLANNING_CORE_VIGIR_FOOTSTEP_PLANNING_DEFAULT_PLUGINS_SRC_WORLD_MODEL_UNEVENTERRAINSTAND_H_
#define VIGIR_FOOTSTEP_PLANNING_CORE_VIGIR_FOOTSTEP_PLANNING_DEFAULT_PLUGINS_SRC_WORLD_MODEL_UNEVENTERRAINSTAND_H_

#include <geometry_msgs/Vector3.h>
#include <multi_contact_point_estimator/uneven_terrain_stand/foot/foot_form.h>
#include <multi_contact_point_estimator/uneven_terrain_stand/foot/foot_state_uneven.h>
#include <multi_contact_point_estimator/uneven_terrain_stand/utilities/multi_contact_point_model_run_lib.h>
#include <multi_contact_point_estimator/uneven_terrain_stand/utilities/q_hull_extended.h>

#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>

#include <vigir_terrain_classifier/grid_map/height_grid_map.h>
#include <vigir_footstep_planning_lib/modeling/state.h>

#include <vector>
#include <mutex>
#include <iostream>

using vigir_footstep_planning::Leg;

class UnevenTerrainStand {
public:
	UnevenTerrainStand(vigir_footstep_planning::State s, geometry_msgs::Vector3 foot_size, vigir_terrain_classifier::HeightGridMap::Ptr height_grid_map, FootForm ff, MultiContactPointModel* const &model, bool use_tensorflow_model);
	virtual ~UnevenTerrainStand();
	FootStateUneven getStand();

private:
	double x;
	double y;
	double yaw;
	Leg leg;
	tf::Pose p; //const tf::Pose& p;
	geometry_msgs::Vector3 foot_size;
	FootForm ff;
	int sampling_steps_x = 10;
	int sampling_steps_y = 10;
	vigir_terrain_classifier::HeightGridMap::Ptr height_grid_map;
	MultiContactPointModel* const &model;
	bool use_tensorflow_model;

	// methods
	void get_points_under_foot(std::vector<orgQhull::vec3> &points);
	FootStateUneven predictStand(std::vector<orgQhull::vec3> const &points, std::vector<double> zmp);
	double rad2deg(double rad);
	bool pointInTriangle(orgQhull::vec3 p, orgQhull::vec3 p0, orgQhull::vec3 p1, orgQhull::vec3 p2);
	void setHeight(FootStateUneven& s, orgQhull::vec3& zmp);
};


#endif /* VIGIR_FOOTSTEP_PLANNING_CORE_VIGIR_FOOTSTEP_PLANNING_DEFAULT_PLUGINS_SRC_WORLD_MODEL_UNEVENTERRAINSTAND_H_ */
