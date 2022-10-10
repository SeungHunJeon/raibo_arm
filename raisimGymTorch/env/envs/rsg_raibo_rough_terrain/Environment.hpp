// Copyright (c) 2020 Robotics and Artificial Intelligence Lab, KAIST
//
// Any unauthorized copying, alteration, distribution, transmission,
// performance, display or use of this material is prohibited.
//
// All rights reserved.

#pragma once

// raisim include
#include "raisim/World.hpp"
#include "raisim/RaisimServer.hpp"

// raisimGymTorch include
#include "../../Yaml.hpp"
#include "../../BasicEigenTypes.hpp"
#include "RaiboController.hpp"
#include "RandomHeightMapGenerator.hpp"
#include "default_controller_demo/module/controller/raibot_default_controller/raibot_default_controller.hpp"

namespace raisim {

class ENVIRONMENT {

 public:

  explicit ENVIRONMENT(const std::string &resourceDir, const Yaml::Node &cfg, bool visualizable, int id) :
      visualizable_(visualizable) {
    setSeed(id);
    world_.addGround();
    world_.setDefaultMaterial(1.1, 0.0, 0.01);
    /// add objects
    raibo_ = world_.addArticulatedSystem(resourceDir + "/raibot/urdf/raibot_simplified.urdf");
    raibo_->setName("robot");
    raibo_->setControlMode(raisim::ControlMode::PD_PLUS_FEEDFORWARD_TORQUE);

    /// Object spawn
    Obj_ = world_.addCylinder(0.5, 0.7, 3);
    Obj_->setName("Obj_");
    Obj_->setPosition(1, 1, 0.35);
    Obj_->setOrientation(1, 0, 0, 0);

    /// create controller
    controller_.create(&world_, Obj_);
    Low_controller_.create(&world_);

    /// set curriculum
    simulation_dt_ = RaiboController::getSimDt();
    control_dt_ = RaiboController::getConDt();

    READ_YAML(double, curriculumFactor_, cfg["curriculum"]["initial_factor"])
    READ_YAML(double, curriculumDecayFactor_, cfg["curriculum"]["decay_factor"])

    /// create heightmap
//    groundType_ = (id+3) % 4;
//    heightMap_ = terrainGenerator_.generateTerrain(&world_, RandomHeightMapGenerator::GroundType(groundType_), curriculumFactor_, gen_, uniDist_);

    /// get robot data
    gcDim_ = int(raibo_->getGeneralizedCoordinateDim());
    gvDim_ = int(raibo_->getDOF());

    /// initialize containers
    gc_init_.setZero(gcDim_);
    gv_init_.setZero(gvDim_);
    nominalJointConfig_.setZero(nJoints_);
    gc_init_from_.setZero(gcDim_);
    gv_init_from_.setZero(gvDim_);

    /// set pd gains
    jointPGain_.setZero(gvDim_);
    jointDGain_.setZero(gvDim_);
    jointPGain_.tail(nJoints_).setConstant(50.0);
    jointDGain_.tail(nJoints_).setConstant(0.5);
    raibo_->setPdGains(jointPGain_, jointDGain_);

    /// this is nominal configuration of anymal
    nominalJointConfig_<< 0, 0.56, -1.12, 0, 0.56, -1.12, 0, 0.56, -1.12, 0, 0.56, -1.12;
    gc_init_.head(7) << 0, 0, 0.4725, 1, 0.0, 0.0, 0.0;
    gc_init_.tail(nJoints_) << nominalJointConfig_;
    gc_init_from_ = gc_init_;
    raibo_->setGeneralizedForce(Eigen::VectorXd::Zero(gvDim_));

    // Reward coefficients
    controller_.setRewardConfig(cfg);



    // visualize if it is the first environment
    if (visualizable_) {
      server_ = std::make_unique<raisim::RaisimServer>(&world_);
      server_->launchServer();
      command_Obj_ = server_->addVisualCylinder("command_Obj_", 0.5, 0.7, 1, 0, 0, 0.5);
//      tar_head_Obj_ = server_->addVisualCylinder("tar_head_Obj_", 0.03, 0.2, 1, 0, 0, 1);
//      cur_head_Obj_ = server_->addVisualCylinder("cur_head_Obj_", 0.03, 0.2, 0, 1, 0, 1);
      command_Obj_Pos_ << 2, 2, 0.35;
      command_Obj_->setPosition(command_Obj_Pos_[0], command_Obj_Pos_[1], command_Obj_Pos_[2]);
//      tar_head_Obj_->setOrientation(sqrt(2)/2, 0, sqrt(2)/2, 0);
//      cur_head_Obj_->setOrientation(sqrt(2)/2, 0, sqrt(2)/2, 0);
    }
  }

  ~ENVIRONMENT() { if (server_) server_->killServer(); }
  void init () { }
  void close () { }
  void setSimulationTimeStep(double dt)
  { controller_.setSimDt(dt);
    Low_controller_.setSimDt(dt);
  }
  void setControlTimeStep(double dt) {
    controller_.setConDt(dt);
    Low_controller_.setConDt(dt);}
  void turnOffVisualization() { server_->hibernate(); }
  void turnOnVisualization() { server_->wakeup(); }
  void startRecordingVideo(const std::string& videoName ) { server_->startRecordingVideo(videoName); }
  void stopRecordingVideo() { server_->stopRecordingVideo(); }
  const std::vector<std::string>& getStepDataTag() { return controller_.getStepDataTag(); }
  const Eigen::VectorXd& getStepData() { return controller_.getStepData(); }

  void reset() {
    /// set the state
    raibo_->setState(gc_init_, gv_init_); /// set it again to ensure that foot is in contact
    updateObstacle();

    controller_.reset(gen_, normDist_, command_Obj_Pos_);
    controller_.updateStateVariables();
    Low_controller_.reset(&world_);
    Low_controller_.updateObservation(&world_);
  }

  double step(const Eigen::Ref<EigenVec>& action, bool visualize) {
    /// action scaling
//    controller_.advance(&world_, action, curriculumFactor_);
    Eigen::Vector3f command = action.cast<float>();
    for (int i = 0; i < 3; i++) {
      if ((command[i]) > 2)
        command[i] = 2;
      if (command[i] < -2)
        command[i] = -2;
    }
    Low_controller_.setCommand(command);


    float dummy;
    int howManySteps;

    for(howManySteps = 0; howManySteps< int(control_dt_ / simulation_dt_ + 1e-10); howManySteps++) {

      subStep();

      if(isTerminalState(dummy)) {
        howManySteps++;
        break;
      }
    }
    return controller_.getRewardSum(visualize);
  }



  void updateObstacle() {
    double x, y, x_command, y_command;
    double phi_;
    phi_ = uniDist_(gen_);
    x = 2.0*cos(phi_*2*M_PI);
    y = 2.0*sin(phi_*2*M_PI);

    x += gc_init_[0];
    y += gc_init_[1];

    Obj_->setPosition(x, y, 0.35);
    Obj_->setOrientation(1, 0, 0, 0);

    double phi;

    phi = uniDist_(gen_);

    x_command = x + sqrt(2)*cos(phi*2*M_PI);
    y_command = y + sqrt(2)*sin(phi*2*M_PI);

    command_Obj_Pos_ << x_command, y_command, 0.35;

    if(visualizable_)
      command_Obj_->setPosition(command_Obj_Pos_[0], command_Obj_Pos_[1], command_Obj_Pos_[2]);

  }



  void subStep() {
//    controller_.updateHistory();
    Low_controller_.updateObservation(&world_);
    Low_controller_.advance(&world_);
    world_.integrate1();
    world_.integrate2();

    controller_.updateStateVariables();
    controller_.accumulateRewards(curriculumFactor_, command_);

  }

  void observe(Eigen::Ref<EigenVec> ob) {
    controller_.updateObservation(true, command_, heightMap_, gen_, normDist_);
    controller_.getObservation(obScaled_);
    ob = obScaled_.cast<float>();
  }

  bool isTerminalState(float& terminalReward) {
//    return controller_.isTerminalState(terminalReward);
    return false;
  }

  void setSeed(int seed) {
    gen_.seed(seed);
    terrainGenerator_.setSeed(seed);
  }

  void curriculumUpdate() {
//    groundType_ = (groundType_+1) % 4; /// rotate ground type for a visualization purpose
    curriculumFactor_ = std::pow(curriculumFactor_, curriculumDecayFactor_);
    /// create heightmap
//    world_.removeObject(heightMap_);
//    heightMap_ = terrainGenerator_.generateTerrain(&world_, RandomHeightMapGenerator::GroundType(groundType_), curriculumFactor_, gen_, uniDist_);
  }

  void moveControllerCursor(Eigen::Ref<EigenVec> pos) {
    controllerSphere_->setPosition(pos[0], pos[1], heightMap_->getHeight(pos[0], pos[1]));
  }

  void setCommand() {
    command_ = controllerSphere_->getPosition();
    commandSphere_->setPosition(command_);
  }

  static constexpr int getObDim() { return RaiboController::getObDim(); }
  static constexpr int getActionDim() { return RaiboController::getActionDim(); }

  void getState(Eigen::Ref<EigenVec> gc, Eigen::Ref<EigenVec> gv) {
    controller_.getState(gc, gv);
  }

 protected:
  static constexpr int nJoints_ = 12;
  raisim::World world_;
  double simulation_dt_;
  double control_dt_;
  int gcDim_, gvDim_;
  std::array<size_t, 4> footFrameIndicies_;

  raisim::ArticulatedSystem* raibo_;
  raisim::HeightMap* heightMap_;
  Eigen::VectorXd gc_init_, gv_init_, nominalJointConfig_;
  Eigen::VectorXd gc_init_from_, gv_init_from_;
  double curriculumFactor_, curriculumDecayFactor_;
  Eigen::VectorXd obScaled_;
  Eigen::Vector3d command_;
  bool visualizable_ = false;
  int groundType_;
  RandomHeightMapGenerator terrainGenerator_;
  RaiboController controller_;
  controller::raibotDefaultController Low_controller_;
  Eigen::VectorXd jointDGain_, jointPGain_;


  std::unique_ptr<raisim::RaisimServer> server_;
  raisim::Visuals *commandSphere_, *controllerSphere_;
  raisim::Cylinder* Obj_;
  raisim::Visuals *command_Obj_, *cur_head_Obj_, *tar_head_Obj_;
  Eigen::Vector3d command_Obj_Pos_;
  Eigen::Vector3d Dist_eo_, Dist_og_;
  raisim::Vec<3> Pos_e_;



  thread_local static std::mt19937 gen_;
  thread_local static std::normal_distribution<double> normDist_;
  thread_local static std::uniform_real_distribution<double> uniDist_;
};

thread_local std::mt19937 raisim::ENVIRONMENT::gen_;
thread_local std::normal_distribution<double> raisim::ENVIRONMENT::normDist_(0., 1.);
thread_local std::uniform_real_distribution<double> raisim::ENVIRONMENT::uniDist_(0., 1.);
}