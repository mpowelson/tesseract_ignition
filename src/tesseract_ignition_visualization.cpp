/**
 * @file tesseract_ignition_vizualization.cpp
 * @brief A tesseract vizualization implementation leveraging Ignition Robotics
 *
 * @author Levi Armstrong
 * @date May 14, 2020
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2020, Southwest Research Institute
 *
 * @par License
 * Software License Agreement (Apache License)
 * @par
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * @par
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <tesseract_command_language/command_language.h>
#include <tesseract_motion_planners/core/utils.h>
#include <tesseract_ignition/tesseract_ignition_visualization.h>
#include <tesseract_ignition/conversions.h>

#include <ignition/transport/Node.hh>
#include <ignition/msgs/MessageTypes.hh>
#include <ignition/common/Console.hh>
#include <ignition/math/eigen3/Conversions.hh>
#include <chrono>

static const std::string DEFAULT_SCENE_TOPIC_NAME =    "/tesseract_ignition/topic/scene"; // ignition::msgs::Scene
static const std::string DEFAULT_POSE_TOPIC_NAME =     "/tesseract_ignition/topic/pose"; // ignition::msgs::Pose_V
static const std::string DEFAULT_DELETION_TOPIC_NAME = "/tesseract_ignition/topic/deletion"; // ignition::msgs::UInt32_V
static const std::string COLLISION_RESULTS_MODEL_NAME = "tesseract_collision_results_model";
static const std::string AXES_MODEL_NAME = "tesseract_axes_model";
static const std::string ARROW_MODEL_NAME = "tesseract_arrow_model";
static const std::string TOOL_PATH_MODEL_NAME = "tesseract_tool_path_model";

using namespace tesseract_ignition;

bool TesseractIgnitionVisualization::init(tesseract::Tesseract::ConstPtr thor)
{
  if (thor == nullptr)
    return false;

  thor_ = std::move(thor);
  scene_pub_ = node_.Advertise<ignition::msgs::Scene>(DEFAULT_SCENE_TOPIC_NAME);
  pose_pub_  = node_.Advertise<ignition::msgs::Pose_V>(DEFAULT_POSE_TOPIC_NAME);
  deletion_pub_  = node_.Advertise<ignition::msgs::Pose_V>(DEFAULT_DELETION_TOPIC_NAME);

  // Wait 10 seconds for a connection to scene topic
  for (int i = 0; i < 10; ++i)
    if (!scene_pub_.HasConnections())
      sleep(1);
    else
      break;

  if (scene_pub_.HasConnections())
  {
    ignition::msgs::Scene msg;
    toMsg(msg, entity_manager_, *(thor_->getEnvironmentConst()->getSceneGraph()), thor_->getEnvironmentConst()->getCurrentState()->link_transforms);

    scene_pub_.Publish(msg);
  }
  else
  {
    return false;
  }

  return true;
}

void TesseractIgnitionVisualization::sendEnvState(const tesseract_environment::EnvState::Ptr& env_state)
{
  ignition::msgs::Pose_V pose_v;
  for (const auto& pair : env_state->link_transforms)
  {
    ignition::msgs::Pose* pose = pose_v.add_pose();
    pose->CopyFrom(ignition::msgs::Convert(ignition::math::eigen3::convert(pair.second)));
    pose->set_name(pair.first);
    pose->set_id(static_cast<unsigned>(entity_manager_.getLink(pair.first)));
  }

  if(!pose_pub_.Publish(pose_v))
  {
    ignerr << "Failed to publish pose vector!" << std::endl;
  }
}

void TesseractIgnitionVisualization::plotTrajectory(const std::vector<std::string>& joint_names,
                                                    const Eigen::Ref<const tesseract_common::TrajArray>& traj)
{
  tesseract_environment::StateSolver::Ptr state_solver = thor_->getEnvironmentConst()->getStateSolver();

  std::chrono::duration<double> fp_s(5.0/static_cast<double>(traj.rows()));
  for (long i = 0; i < traj.rows(); ++i)
  {
    tesseract_environment::EnvState::Ptr state = state_solver->getState(joint_names, traj.row(i));
    sendEnvState(state);
    std::this_thread::sleep_for(fp_s);
  }
}

void TesseractIgnitionVisualization::plotTrajectory(const tesseract_common::JointTrajectory& traj)
{
  plotTrajectory(traj.joint_names, traj.trajectory);
}

void TesseractIgnitionVisualization::plotTrajectory(const tesseract_planning::Instruction& instruction)
{
  using namespace tesseract_planning;
  tesseract_environment::StateSolver::Ptr state_solver = thor_->getEnvironmentConst()->getStateSolver();
  std::chrono::duration<double> fp_s(0.1);
  double prev_time = 0;
  if (isCompositeInstruction(instruction))
  {
    const auto* ci = instruction.cast_const<CompositeInstruction>();

    std::vector<std::reference_wrapper<const Instruction>> fi = flatten(*ci, moveFilter);
    for (const auto& i : fi)
    {
      assert(isMoveInstruction(i.get()));
      std::this_thread::sleep_for(fp_s);
      const auto* mi = i.get().cast_const<MoveInstruction>();
      if (isStateWaypoint(mi->getWaypoint()))
      {
        const auto* swp = mi->getWaypoint().cast_const<StateWaypoint>();
        double dt = swp->time - prev_time;
        if (dt > 0)
          std::this_thread::sleep_for(std::chrono::duration<double>(dt));
        else
          std::this_thread::sleep_for(fp_s);

        assert(static_cast<long>(swp->joint_names.size()) == swp->position.size());
        tesseract_environment::EnvState::Ptr state = state_solver->getState(swp->joint_names, swp->position);
        sendEnvState(state);
      }
      else if (isJointWaypoint(mi->getWaypoint()))
      {
        std::this_thread::sleep_for(fp_s);
        const auto* jwp = mi->getWaypoint().cast_const<JointWaypoint>();
        assert(static_cast<long>(jwp->joint_names.size()) == jwp->size());
        tesseract_environment::EnvState::Ptr state = state_solver->getState(jwp->joint_names, *jwp);
        sendEnvState(state);
      }
      else
      {
        ignerr << "plotTrajectoy: Unsupported Waypoint Type!" << std::endl;
      }
    }
  }
  else if (isMoveInstruction(instruction))
  {
    std::this_thread::sleep_for(fp_s);
    const auto* mi = instruction.cast_const<MoveInstruction>();
    if (isStateWaypoint(mi->getWaypoint()))
    {
      const auto* swp = mi->getWaypoint().cast_const<StateWaypoint>();
      double dt = swp->time - prev_time;
      if (dt > 0)
        std::this_thread::sleep_for(std::chrono::duration<double>(dt));
      else
        std::this_thread::sleep_for(fp_s);

      assert(static_cast<long>(swp->joint_names.size()) == swp->position.size());
      tesseract_environment::EnvState::Ptr state = state_solver->getState(swp->joint_names, swp->position);
      sendEnvState(state);
    }
    else if (isJointWaypoint(mi->getWaypoint()))
    {
      std::this_thread::sleep_for(fp_s);
      const auto* jwp = mi->getWaypoint().cast_const<JointWaypoint>();
      assert(static_cast<long>(jwp->joint_names.size()) == jwp->size());
      tesseract_environment::EnvState::Ptr state = state_solver->getState(jwp->joint_names, *jwp);
      sendEnvState(state);
    }
    else
    {
      ignerr << "plotTrajectoy: Unsupported Waypoint Type!" << std::endl;
    }
  }
  else
  {
    ignerr << "plotTrajectoy: Unsupported Instruction Type!" << std::endl;
  }
}

void addArrow(EntityManager& entity_manager,
              ignition::msgs::Link& link,
              long& sub_index,
              const std::string& parent_name,
              const Eigen::Ref<const Eigen::Vector3d>& pt1,
              const Eigen::Ref<const Eigen::Vector3d>& pt2,
              const Eigen::Ref<const Eigen::Vector4d>& rgba,
              double radius)
{
  std::string gv_name = parent_name + "_" + std::to_string(++sub_index);
  ignition::msgs::Visual* gv_msg = link.add_visual();
  gv_msg->set_id(static_cast<unsigned>(entity_manager.addVisual(gv_name)));
  gv_msg->set_name(gv_name);

  Eigen::Isometry3d pose = Eigen::Isometry3d::Identity();
  Eigen::Vector3d x, y, z;
  z = (pt2 - pt1).normalized();
  y = z.unitOrthogonal();
  x = (y.cross(z)).normalized();
  Eigen::Matrix3d rot;
  rot.col(0) = x;
  rot.col(1) = y;
  rot.col(2) = z;
  pose.linear() = rot;
  pose.translation() = pt1 + (((pt2 - pt1).norm() / 2) * z);

  gv_msg->mutable_pose()->CopyFrom(ignition::msgs::Convert(ignition::math::eigen3::convert(pose)));

  ignition::msgs::Geometry geometry_msg;
  geometry_msg.set_type(ignition::msgs::Geometry::Type::Geometry_Type_CYLINDER);
  ignition::msgs::CylinderGeom shape_geometry_msg;
  shape_geometry_msg.set_radius(radius);
  shape_geometry_msg.set_length((pt2 - pt1).norm());
  geometry_msg.mutable_cylinder()->CopyFrom(shape_geometry_msg);
  gv_msg->mutable_geometry()->CopyFrom(geometry_msg);
  ignition::msgs::Material shape_material_msg;
  shape_material_msg.mutable_diffuse()->set_r(static_cast<float>(rgba(0)));
  shape_material_msg.mutable_diffuse()->set_g(static_cast<float>(rgba(1)));
  shape_material_msg.mutable_diffuse()->set_b(static_cast<float>(rgba(2)));
  shape_material_msg.mutable_diffuse()->set_a(static_cast<float>(rgba(3)));
  gv_msg->mutable_material()->CopyFrom(shape_material_msg);
  gv_msg->set_parent_name(parent_name);
}

void addCylinder(EntityManager& entity_manager,
                 ignition::msgs::Link& link,
                 long& sub_index,
                 const std::string& parent_name,
                 const Eigen::Ref<const Eigen::Vector3d>& pt1,
                 const Eigen::Ref<const Eigen::Vector3d>& pt2,
                 const Eigen::Ref<const Eigen::Vector4d>& rgba,
                 double radius)
{
  std::string gv_name = parent_name + "_" + std::to_string(++sub_index);
  ignition::msgs::Visual* gv_msg = link.add_visual();
  gv_msg->set_id(static_cast<unsigned>(entity_manager.addVisual(gv_name)));
  gv_msg->set_name(gv_name);

  Eigen::Isometry3d pose = Eigen::Isometry3d::Identity();
  Eigen::Vector3d x, y, z;
  z = (pt2 - pt1).normalized();
  y = z.unitOrthogonal();
  x = (y.cross(z)).normalized();
  Eigen::Matrix3d rot;
  rot.col(0) = x;
  rot.col(1) = y;
  rot.col(2) = z;
  pose.linear() = rot;
  pose.translation() = pt1 + (((pt2 - pt1).norm() / 2) * z);

  gv_msg->mutable_pose()->CopyFrom(ignition::msgs::Convert(ignition::math::eigen3::convert(pose)));

  ignition::msgs::Geometry geometry_msg;
  geometry_msg.set_type(ignition::msgs::Geometry::Type::Geometry_Type_CYLINDER);
  ignition::msgs::CylinderGeom shape_geometry_msg;
  shape_geometry_msg.set_radius(radius);
  shape_geometry_msg.set_length((pt2 - pt1).norm());
  geometry_msg.mutable_cylinder()->CopyFrom(shape_geometry_msg);
  gv_msg->mutable_geometry()->CopyFrom(geometry_msg);
  ignition::msgs::Material shape_material_msg;
  shape_material_msg.mutable_diffuse()->set_r(static_cast<float>(rgba(0)));
  shape_material_msg.mutable_diffuse()->set_g(static_cast<float>(rgba(1)));
  shape_material_msg.mutable_diffuse()->set_b(static_cast<float>(rgba(2)));
  shape_material_msg.mutable_diffuse()->set_a(static_cast<float>(rgba(3)));
  gv_msg->mutable_material()->CopyFrom(shape_material_msg);
  gv_msg->set_parent_name(parent_name);
}

void addAxis(EntityManager& entity_manager,
             ignition::msgs::Link& link,
             long& sub_index,
             const std::string& parent_name,
             const Eigen::Isometry3d& axis,
             double scale)
{
  Eigen::Vector3d x_axis = axis.matrix().block<3, 1>(0, 0);
  Eigen::Vector3d y_axis = axis.matrix().block<3, 1>(0, 1);
  Eigen::Vector3d z_axis = axis.matrix().block<3, 1>(0, 2);
  Eigen::Vector3d position = axis.matrix().block<3, 1>(0, 3);

  std::string gv_name = parent_name + "_" + std::to_string(++sub_index);
  addCylinder(entity_manager, link, sub_index, parent_name, position, position + (scale * x_axis), Eigen::Vector4d(1, 0, 0, 1), scale * (1.0 / 20));
  addCylinder(entity_manager, link, sub_index, parent_name, position, position + (scale * y_axis), Eigen::Vector4d(0, 1, 0, 1), scale * (1.0 / 20));
  addCylinder(entity_manager, link, sub_index, parent_name, position, position + (scale * z_axis), Eigen::Vector4d(0, 0, 1, 1), scale * (1.0 / 20));
}

void TesseractIgnitionVisualization::plotToolPath(const tesseract_planning::Instruction& instruction)
{
  using namespace tesseract_planning;

  ignition::msgs::Scene scene_msg;
  scene_msg.set_name("scene");
  ignition::msgs::Model* model = scene_msg.add_model();
  std::string model_name = TOOL_PATH_MODEL_NAME;
  model->set_name(model_name);
  model->set_id(static_cast<unsigned>(entity_manager_.addModel(model_name)));

  tesseract_environment::StateSolver::Ptr state_solver = thor_->getEnvironmentConst()->getStateSolver();
  if (isCompositeInstruction(instruction))
  {
    const auto* ci = instruction.cast_const<CompositeInstruction>();

    // Assume all the plan instructions have the same manipulator as the composite
    assert(!ci->getManipulatorInfo().isEmpty());
    const ManipulatorInfo& composite_mi = ci->getManipulatorInfo();
    const std::string& manipulator = composite_mi.manipulator;
    const Eigen::Isometry3d& tcp = composite_mi.tcp;
    const std::string& working_frame = composite_mi.working_frame;

    auto composite_mi_fwd_kin = thor_->getFwdKinematicsManagerConst()->getFwdKinematicSolver(manipulator);
    if (composite_mi_fwd_kin == nullptr)
    {
      ignerr << "plotToolPath: Manipulator: " << manipulator << " does not exist!" << std::endl;
      return;
    }
    const std::string& tip_link = composite_mi_fwd_kin->getTipLinkName();

    std::vector<std::reference_wrapper<const Instruction>> fi = flatten(*ci, planFilter);
    long cnt = 0;
    for (const auto& i : fi)
    {
      std::string link_name = model_name + std::to_string(++cnt);
      ignition::msgs::Link* link_msg = model->add_link();
      link_msg->set_id(static_cast<unsigned>(entity_manager_.addVisual(link_name)));
      link_msg->set_name(link_name);

      assert(isPlanInstruction(i.get()));
      const auto* pi = i.get().cast_const<PlanInstruction>();
      if (isStateWaypoint(pi->getWaypoint()))
      {
        const auto* swp = pi->getWaypoint().cast_const<StateWaypoint>();
        assert(static_cast<long>(swp->joint_names.size()) == swp->position.size());
        tesseract_environment::EnvState::Ptr state = state_solver->getState(swp->joint_names, swp->position);
        addAxis(entity_manager_, *link_msg, cnt, link_name, state->link_transforms[tip_link] * tcp, 1);
      }
      else if (isJointWaypoint(pi->getWaypoint()))
      {
        const auto* jwp = pi->getWaypoint().cast_const<JointWaypoint>();
        assert(static_cast<long>(jwp->joint_names.size()) == jwp->size());
        tesseract_environment::EnvState::Ptr state = state_solver->getState(jwp->joint_names, *jwp);
        addAxis(entity_manager_, *link_msg, cnt, link_name, state->link_transforms[tip_link] * tcp, 1);
      }
      else if (isCartesianWaypoint(pi->getWaypoint()))
      {
        const auto* cwp = pi->getWaypoint().cast_const<CartesianWaypoint>();
        if (working_frame.empty())
        {
          addAxis(entity_manager_, *link_msg, cnt, link_name, (*cwp) * tcp, 1);
        }
        else
        {
          tesseract_environment::EnvState::ConstPtr state = thor_->getEnvironmentConst()->getCurrentState();
          addAxis(entity_manager_, *link_msg, cnt, link_name, state->link_transforms.at(working_frame) * (*cwp) * tcp, 1);
        }
      }
      else
      {
        ignerr << "plotTrajectoy: Unsupported Waypoint Type!" << std::endl;
      }
    }
  }
  else if (isPlanInstruction(instruction))
  {

    long cnt = 0;
    std::string link_name = model_name + std::to_string(++cnt);
    ignition::msgs::Link* link_msg = model->add_link();
    link_msg->set_id(static_cast<unsigned>(entity_manager_.addVisual(link_name)));
    link_msg->set_name(link_name);

    assert(isPlanInstruction(instruction));
    const auto* pi = instruction.cast_const<PlanInstruction>();

    // Assume all the plan instructions have the same manipulator as the composite
    assert(!pi->getManipulatorInfo().isEmpty());
    const ManipulatorInfo& composite_mi = pi->getManipulatorInfo();
    const std::string& manipulator = composite_mi.manipulator;
    const Eigen::Isometry3d& tcp = composite_mi.tcp;
    const std::string& working_frame = composite_mi.working_frame;

    auto composite_mi_fwd_kin = thor_->getFwdKinematicsManagerConst()->getFwdKinematicSolver(manipulator);
    if (composite_mi_fwd_kin == nullptr)
    {
      ignerr << "plotToolPath: Manipulator: " << manipulator << " does not exist!" << std::endl;
      return;
    }
    const std::string& tip_link = composite_mi_fwd_kin->getTipLinkName();

    if (isStateWaypoint(pi->getWaypoint()))
    {
      const auto* swp = pi->getWaypoint().cast_const<StateWaypoint>();
      assert(static_cast<long>(swp->joint_names.size()) == swp->position.size());
      tesseract_environment::EnvState::Ptr state = state_solver->getState(swp->joint_names, swp->position);
      addAxis(entity_manager_, *link_msg, cnt, link_name, state->link_transforms[tip_link] * tcp, 1);
    }
    else if (isJointWaypoint(pi->getWaypoint()))
    {
      const auto* jwp = pi->getWaypoint().cast_const<JointWaypoint>();
      assert(static_cast<long>(jwp->joint_names.size()) == jwp->size());
      tesseract_environment::EnvState::Ptr state = state_solver->getState(jwp->joint_names, *jwp);
      addAxis(entity_manager_, *link_msg, cnt, link_name, state->link_transforms[tip_link] * tcp, 1);
    }
    else if (isCartesianWaypoint(pi->getWaypoint()))
    {
      const auto* cwp = pi->getWaypoint().cast_const<CartesianWaypoint>();
      if (working_frame.empty())
      {
        addAxis(entity_manager_, *link_msg, cnt, link_name, (*cwp) * tcp, 1);
      }
      else
      {
        tesseract_environment::EnvState::ConstPtr state = thor_->getEnvironmentConst()->getCurrentState();
        addAxis(entity_manager_, *link_msg, cnt, link_name, state->link_transforms.at(working_frame) * (*cwp) * tcp, 1);
      }
    }
    else
    {
      ignerr << "plotTrajectoy: Unsupported Waypoint Type!" << std::endl;
    }
  }
  else
  {
    ignerr << "plotTrajectoy: Unsupported Instruction Type!" << std::endl;
  }
}

void TesseractIgnitionVisualization::plotContactResults(const std::vector<std::string>& link_names,
                                                        const tesseract_collision::ContactResultVector& dist_results,
                                                        const Eigen::Ref<const Eigen::VectorXd>& safety_distances)
{
  ignition::msgs::Scene scene_msg;
  scene_msg.set_name("scene");
  ignition::msgs::Model* model = scene_msg.add_model();
  std::string model_name = COLLISION_RESULTS_MODEL_NAME;
  model->set_name(model_name);
  model->set_id(static_cast<unsigned>(entity_manager_.addModel(model_name)));

  long cnt = 0;
  for (size_t i = 0; i < dist_results.size(); ++i)
  {
    const tesseract_collision::ContactResult& dist = dist_results[i];
    const double& safety_distance = safety_distances[static_cast<long>(i)];

    std::string link_name = model_name + std::to_string(++cnt);
    ignition::msgs::Link* link_msg = model->add_link();
    link_msg->set_id(static_cast<unsigned>(entity_manager_.addVisual(link_name)));
    link_msg->set_name(link_name);

    Eigen::Vector4d rgba;
    if (dist.distance < 0)
    {
      rgba << 1.0, 0.0, 0.0, 1.0;
    }
    else if (dist.distance < safety_distance)
    {
      rgba << 1.0, 1.0, 0.0, 1.0;
    }
    else
    {
      rgba << 0.0, 1.0, 0.0, 1.0;
    }

    if (dist.cc_type[0] == tesseract_collision::ContinuousCollisionType::CCType_Between)
    {
      Eigen::Vector4d cc_rgba;
      cc_rgba << 0.0, 0.0, 1.0, 1.0;
      addArrow(entity_manager_, *link_msg, cnt,
               link_name, dist.transform[0] * dist.nearest_points_local[0],
               dist.cc_transform[0] * dist.nearest_points_local[0], cc_rgba, 0.2);
    }

    if (dist.cc_type[1] == tesseract_collision::ContinuousCollisionType::CCType_Between)
    {
      Eigen::Vector4d cc_rgba;
      cc_rgba << 0.0, 0.0, 0.5, 1.0;
      addArrow(entity_manager_, *link_msg, cnt,
               link_name, dist.transform[1] * dist.nearest_points_local[1],
               dist.cc_transform[1] * dist.nearest_points_local[1], cc_rgba, 0.2);
    }

    auto it0 = std::find(link_names.begin(), link_names.end(), dist.link_names[0]);
    auto it1 = std::find(link_names.begin(), link_names.end(), dist.link_names[1]);

    if (it0 != link_names.end() && it1 != link_names.end())
    {
      addArrow(entity_manager_, *link_msg, cnt, link_name, dist.nearest_points[0], dist.nearest_points[1], rgba, 0.2);
      addArrow(entity_manager_, *link_msg, cnt, link_name, dist.nearest_points[1], dist.nearest_points[0], rgba, 0.2);
    }
    else if (it0 != link_names.end())
    {
      addArrow(entity_manager_, *link_msg, cnt, link_name, dist.nearest_points[1], dist.nearest_points[0], rgba, 0.2);
    }
    else
    {
      addArrow(entity_manager_, *link_msg, cnt, link_name, dist.nearest_points[0], dist.nearest_points[1], rgba, 0.2);
    }
  }
  scene_pub_.Publish(scene_msg);
}

void TesseractIgnitionVisualization::plotArrow(const Eigen::Ref<const Eigen::Vector3d>& pt1,
                                               const Eigen::Ref<const Eigen::Vector3d>& pt2,
                                               const Eigen::Ref<const Eigen::Vector4d>& rgba,
                                               double scale)
{
  ignition::msgs::Scene scene_msg;
  scene_msg.set_name("scene");
  ignition::msgs::Model* model = scene_msg.add_model();
  std::string model_name = ARROW_MODEL_NAME;
  model->set_name(model_name);
  model->set_id(static_cast<unsigned>(entity_manager_.addModel(model_name)));

  long cnt = 0;
  std::string link_name = model_name + std::to_string(++cnt);
  ignition::msgs::Link* link_msg = model->add_link();
  link_msg->set_id(static_cast<unsigned>(entity_manager_.addVisual(link_name)));
  link_msg->set_name(link_name);
  addArrow(entity_manager_, *link_msg, cnt, link_name, pt1, pt2, rgba, scale * ((pt2 - pt1).norm() * (1.0 / 20)));
  scene_pub_.Publish(scene_msg);
}

void TesseractIgnitionVisualization::plotAxis(const Eigen::Isometry3d& axis, double scale)
{
  ignition::msgs::Scene scene_msg;
  scene_msg.set_name("scene");
  ignition::msgs::Model* model = scene_msg.add_model();
  std::string model_name = AXES_MODEL_NAME;
  model->set_name(model_name);
  model->set_id(static_cast<unsigned>(entity_manager_.addModel(model_name)));

  long cnt = 0;
  std::string link_name = model_name + std::to_string(++cnt);
  ignition::msgs::Link* link_msg = model->add_link();
  link_msg->set_id(static_cast<unsigned>(entity_manager_.addVisual(link_name)));
  link_msg->set_name(link_name);
  addAxis(entity_manager_, *link_msg, cnt, link_name, axis, scale);
  scene_pub_.Publish(scene_msg);
}

void TesseractIgnitionVisualization::clear()
{
  ignition::msgs::UInt32_V deletion_msg;
  long id = entity_manager_.getModel(COLLISION_RESULTS_MODEL_NAME);
  if (id >= 1000)
    deletion_msg.add_data(static_cast<unsigned>(id));

  id = entity_manager_.getModel(ARROW_MODEL_NAME);
  if (id >= 1000)
    deletion_msg.add_data(static_cast<unsigned>(id));

  id = entity_manager_.getModel(AXES_MODEL_NAME);
  if (id >= 1000)
    deletion_msg.add_data(static_cast<unsigned>(id));

  deletion_pub_.Publish(deletion_msg);
}

void TesseractIgnitionVisualization::waitForInput()
{
  std::cout << "Hit enter key to continue!" << std::endl;
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

