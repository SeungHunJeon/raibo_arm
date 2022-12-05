//
// Created by jemin on 2/25/20.
//

#ifndef _RAISIM_GYM_RAIBO_CONTROLLER_HPP
#define _RAISIM_GYM_RAIBO_CONTROLLER_HPP

#include "unsupported/Eigen/MatrixFunctions"
#include "raisim/RaisimServer.hpp"


Eigen::Matrix3d hat(const Eigen::Vector3d R3) {
  Eigen::Matrix3d so3;
  so3 << 0, -R3(2), R3(1),
      R3(2), 0, -R3(0),
      -R3(1), R3(0), 0;

  return so3;
}

Eigen::Vector3d vee(const Eigen::Matrix3d so3) {
  Eigen::Vector3d R3;
  R3 << so3(2,1), -so3(2,0), so3(1,0);

  return R3;
}

Eigen::Matrix3d log(const Eigen::Matrix3d SO3) {
  Eigen::Matrix3d so3;
  so3 = SO3.log();

  return so3;
}

Eigen::Vector3d LOG(const Eigen::MatrixXd SO3) {
  Eigen::Vector3d R3;
  R3 = vee(SO3.log());

  return R3;
}

Eigen::Matrix3d exp(const Eigen::Matrix3d so3) {
  Eigen::Matrix3d SO3;
  SO3 = so3.exp();

  return SO3;
}

Eigen::Matrix3d EXP(const Eigen::Vector3d R3) {
  Eigen::Matrix3d SO3;
  SO3 = hat(R3).exp();

  return SO3;
}

namespace raisim {

class RaiboController {
 public:
  inline bool create(raisim::World *world, raisim::SingleBodyObject *obj) {
    raibo_ = reinterpret_cast<raisim::ArticulatedSystem *>(world->getObject("robot"));
    gc_.resize(raibo_->getGeneralizedCoordinateDim());
    gv_.resize(raibo_->getDOF());
    jointVelocity_.resize(nJoints_);
    nominalConfig_.setZero(nJoints_);
    nominalConfig_ << 0.0, 0.559836, -1.119672, -0.0, 0.559836, -1.119672, 0.0, 0.559836, -1.119672, -0.0, 0.559836, -1.119672;

    Obj_ = obj;
    /// foot scan config

    /// Observation
    actionTarget_.setZero(actionDim_);

    /// action
    actionMean_.setZero(actionDim_);
    actionStd_.setZero(actionDim_);
    actionScaled_.setZero(actionDim_);

    actionMean_ << Eigen::VectorXd::Constant(actionDim_, 0.0); /// joint target
    actionStd_<< Eigen::VectorXd::Constant(actionDim_, 0.5); /// joint target

    obMean_.setZero(obDim_);
    obStd_.setZero(obDim_);
    obDouble_.setZero(obDim_);


    Obj_Info_.setZero(exteroceptiveDim_);

    // Update History
    objectInfoHistory_.resize(historyNum_);
    stateInfoHistory_.resize(historyNum_);
    actionInfoHistory_.resize(actionNum_);

    for (int i =0; i<historyNum_; i++) {
      objectInfoHistory_[i].setZero(exteroceptiveDim_);
      stateInfoHistory_[i].setZero(proprioceptiveDim_);

    }

    for (int i = 0; i<actionNum_; i++)
      actionInfoHistory_[i].setZero(actionDim_);

    for (int i=0; i<historyNum_+1 ; i++) {
      obMean_.segment((proprioceptiveDim_ + exteroceptiveDim_ + actionDim_)*i, proprioceptiveDim_ + exteroceptiveDim_ + actionDim_) <<
        0.0, 0.0, 1.4, /// gravity axis 3
        Eigen::VectorXd::Constant(6, 0.0), /// body lin/ang vel 6
        Eigen::VectorXd::Constant(2, 0),
        Eigen::VectorXd::Constant(1, 2), /// end-effector to object distance
        Eigen::VectorXd::Constant(2, 0),
        Eigen::VectorXd::Constant(1, sqrt(2)), /// object to target distance
        Eigen::VectorXd::Constant(2, 0),
        Eigen::VectorXd::Constant(1, 2), /// end-effector to target distance
        Eigen::VectorXd::Constant(3, 0.0), /// object to target velocity
        Eigen::VectorXd::Constant(3, 0.0), /// object to target angular velocity
        Eigen::VectorXd::Constant(1, 2), /// mass
        Eigen::VectorXd::Constant(3, 0), /// COM
        Eigen::VectorXd::Constant(9, 0), /// Inertia
        0.0, 0.0, 1.4, /// Orientation
        Eigen::VectorXd::Constant(4,0.5), /// one hot vector
        1.0, 1.0, 1.0, /// object geometry
        Eigen::VectorXd::Constant(actionDim_, 0.0);
    }

    for (int i=0; i<historyNum_+1 ; i++) {
      obStd_.segment((proprioceptiveDim_ + exteroceptiveDim_ + actionDim_)*i, proprioceptiveDim_ + exteroceptiveDim_ + actionDim_) <<
        Eigen::VectorXd::Constant(3, 0.3), /// gravity axes
        Eigen::VectorXd::Constant(3, 0.6), /// linear velocity
        Eigen::VectorXd::Constant(3, 1.0), /// angular velocities
        Eigen::VectorXd::Constant(2, 0.5),
        Eigen::VectorXd::Constant(1, 0.6), /// end-effector to object distance
        Eigen::VectorXd::Constant(2, 0.5),
        Eigen::VectorXd::Constant(1, 0.6), /// object to target distance
        Eigen::VectorXd::Constant(2, 0.5),
        Eigen::VectorXd::Constant(1, 0.6), /// end-effector to target distance
        Eigen::VectorXd::Constant(3, 0.5), /// object to target velocity
        Eigen::VectorXd::Constant(3, 0.5), /// object to angular velocity
        Eigen::VectorXd::Constant(1, 0.2),
        Eigen::VectorXd::Constant(3, 0.5),
        Eigen::VectorXd::Constant(9, 0.2),
        Eigen::VectorXd::Constant(3,0.3), /// Orientation
        Eigen::VectorXd::Constant(4,0.2), /// one hot vector
        0.2, 0.2, 0.2, /// object geometry
        Eigen::VectorXd::Constant(actionDim_, 0.5);
    }

    footIndices_.push_back(raibo_->getBodyIdx("LF_SHANK"));
    footIndices_.push_back(raibo_->getBodyIdx("RF_SHANK"));
    footIndices_.push_back(raibo_->getBodyIdx("LH_SHANK"));
    footIndices_.push_back(raibo_->getBodyIdx("RH_SHANK"));
    RSFATAL_IF(std::any_of(footIndices_.begin(), footIndices_.end(), [](int i){return i < 0;}), "footIndices_ not found")

    /// exported data
    stepDataTag_ = {"towardObject_rew",
                    "stayObject_rew",
                    "towardTarget_rew",
                    "stayTarget_rew",
                    "command_rew",
                    "torque_rew",
                    "stayObject_heading_rew"};
    stepData_.resize(stepDataTag_.size());


    classify_vector_.setZero(4);
    classify_vector_ << 1, 0, 0, 0;

    return true;
  };

  void updateHistory() {
    /// joint angles

    std::rotate(objectInfoHistory_.begin(), objectInfoHistory_.begin()+1, objectInfoHistory_.end());
    objectInfoHistory_[historyNum_ - 1] = Obj_Info_;

    Eigen::VectorXd stateInfo;
    stateInfo.setZero(proprioceptiveDim_);
    stateInfo.head(3) = baseRot_.e().row(2);
    stateInfo.segment(3,3) = bodyLinVel_;
    stateInfo.segment(6,3) = bodyAngVel_;

    std::rotate(stateInfoHistory_.begin(), stateInfoHistory_.begin()+1, stateInfoHistory_.end());
    stateInfoHistory_[historyNum_ - 1] = stateInfo;

  }

  void updateStateVariables() {
    raibo_->getState(gc_, gv_);
    jointVelocity_ = gv_.tail(nJoints_);

    raisim::Vec<4> quat;
    quat[0] = gc_[3];
    quat[1] = gc_[4];
    quat[2] = gc_[5];
    quat[3] = gc_[6];
    raisim::quatToRotMat(quat, baseRot_);
    bodyLinVel_ = baseRot_.e().transpose() * gv_.segment(0, 3);
    bodyAngVel_ = baseRot_.e().transpose() * gv_.segment(3, 3);

    raibo_->getFramePosition(raibo_->getFrameIdxByLinkName("arm_link"), ee_Pos_w_);
    raibo_->getFrameVelocity(raibo_->getFrameIdxByLinkName("arm_link"), ee_Vel_w_);

    /// Object info

    Obj_->getPosition(Obj_Pos_);
    Obj_->getLinearVelocity(Obj_Vel_);
    Obj_->getAngularVelocity(Obj_AVel_);
    //TODO

    Eigen::Vector3d ee_to_obj = (Obj_Pos_.e()-ee_Pos_w_.e());
    Eigen::Vector3d obj_to_target (command_Obj_Pos_ - Obj_Pos_.e());
    Eigen::Vector3d ee_to_target = (command_Obj_Pos_ - Obj_Pos_.e());
    ee_to_obj(2) = 0;
    obj_to_target(2) = 0;
    ee_to_target(2) = 0;
    ee_to_obj = baseRot_.e().transpose() * ee_to_obj;
    obj_to_target = baseRot_.e().transpose() * obj_to_target;
    ee_to_target = baseRot_.e().transpose() * ee_to_target;

    Eigen::Vector2d pos_temp_;
    double dist_temp_;

    dist_temp_ = ee_to_obj.head(2).norm();
    pos_temp_ = ee_to_obj.head(2) * (1./dist_temp_);

    Obj_Info_.segment(0, 2) << pos_temp_;
    Obj_Info_.segment(2, 1) << std::min(2., dist_temp_);

    dist_temp_ = obj_to_target.head(2).norm();
    pos_temp_ = obj_to_target.head(2) * (1./dist_temp_);

    Obj_Info_.segment(3, 2) << pos_temp_;
    Obj_Info_.segment(5, 1) << std::min(2., dist_temp_);

    dist_temp_ = ee_to_target.head(2).norm();
    pos_temp_ = ee_to_target.head(2) * (1./dist_temp_);

    Obj_Info_.segment(6, 2) << pos_temp_;
    Obj_Info_.segment(8, 1) << std::min(2., dist_temp_);

    Obj_Info_.segment(9, 3) << baseRot_.e().transpose() * Obj_Vel_.e();

    Obj_Info_.segment(12, 3) << baseRot_.e().transpose() * Obj_AVel_.e();

    Obj_Info_.segment(15, 1) << Obj_->getMass();

    Obj_Info_.segment(16, 3) << Obj_->getCom().e();

    Obj_Info_.segment(19,3) = Obj_->getInertiaMatrix_B().row(0);

    Obj_Info_.segment(22,3) = Obj_->getInertiaMatrix_B().row(1);

    Obj_Info_.segment(25,3) = Obj_->getInertiaMatrix_B().row(2);

    Obj_Info_.segment(28,3) = Obj_->getOrientation().e().row(2);

    Obj_Info_.segment(31,4) = classify_vector_;

    Obj_Info_.segment(35,3) = obj_geometry_;

    /// height map
    controlFrameX_ =
        {baseRot_[0], baseRot_[1], 0.}; /// body x axis projected on the world x-y plane, expressed in the world frame
    controlFrameX_ /= controlFrameX_.norm();
    raisim::cross(zAxis_, controlFrameX_, controlFrameY_);

    /// check if the feet are in contact with the ground
  }

  void getObservation(Eigen::VectorXd &observation) {
    observation = (obDouble_ - obMean_).cwiseQuotient(obStd_);
  }

  Eigen::VectorXf advance(raisim::World *world, const Eigen::Ref<EigenVec> &action) {
    Eigen::VectorXf position = action.cast<float>().cwiseQuotient(actionStd_.cast<float>());
    position += actionMean_.cast<float>();
    command_ = {position(0), position(1), 0};
    return command_;
  }

  bool advance(raisim::World *world, const Eigen::Ref<EigenVec> &action, double curriculumFactor) {
    /// action scaling
    std::rotate(actionInfoHistory_.begin(), actionInfoHistory_.begin()+1, actionInfoHistory_.end());
    actionInfoHistory_[actionNum_ - 1] = action.cast<double>();
//    actionTarget_ = action.cast<double>();
//
//    jointTarget_.head(nlegJoints_) << actionTarget_.head(nlegJoints_).cwiseProduct(actionStd_.head(nlegJoints_));
//    jointTarget_.head(nlegJoints_) += actionMean_.head(nlegJoints_);
//
//    pTarget_.segment(7,nlegJoints_) = jointTarget_.head(nlegJoints_);
//    raibo_->setPdTarget(pTarget_, vTarget_);

    /// Variable impedance control
//    Eigen::VectorXd PDgainTarget_, PDgainTarget_exp_;
//    PDgainTarget_.setZero(nVargain_);
//    PDgainTarget_exp_.setZero(nVargain_);
//    PDgainTarget_ = actionTarget_.tail(nVargain_).cwiseProduct(actionStd_.tail(nVargain_));
//    PDgainTarget_exp_ = PDgainTarget_.array().exp();
//    PDgainTarget_ = actionMean_.tail(nVargain_).cwiseProduct(PDgainTarget_exp_);

//    posPgain_ = PDgainTarget_[0];
//    posDgain_ = PDgainTarget_[1];
//    oriPgain_ = PDgainTarget_[2];
//    oriDgain_ = PDgainTarget_[3];

//    smoothReward_ = curriculumFactor * smoothRewardCoeff_ * (prevprevAction_ + jointTarget_ - 2 * previousAction_).squaredNorm();
    return true;
  }

  void reset(std::mt19937 &gen_,
             std::normal_distribution<double> &normDist_, Eigen::Vector3d command_obj_pos_, Eigen::Vector3d obj_geometry) {
    raibo_->getState(gc_, gv_);
//    jointTarget_ = gc_.segment(7, nJoints_);
    command_Obj_Pos_ = command_obj_pos_;
    obj_geometry_ = obj_geometry;


    // history
    for (int i = 0; i < historyNum_; i++)
    {
      for (int j=0; j < exteroceptiveDim_; j++)
        objectInfoHistory_[i](j) = normDist_(gen_) * 0.1;

      for (int j=0; j < proprioceptiveDim_; j++)
        stateInfoHistory_[i](j) = normDist_(gen_) * 0.1;
    }

    for (int i = 0; i < actionNum_; i++)
      for (int j=0; j < actionDim_; j++)
        actionInfoHistory_[i](j) = normDist_(gen_) * 0.1;

  }

  [[nodiscard]] float getRewardSum(bool visualize) {
    stepData_[0] = towardObjectReward_;
    stepData_[1] = stayObjectReward_;
    stepData_[2] = towardTargetReward_;
    stepData_[3] = stayTargetReward_;
    stepData_[4] = commandReward_;
    stepData_[5] = torqueReward_;
    stepData_[6] = stayObjectHeadingReward_;

    towardObjectReward_ = 0.;
    stayObjectReward_ = 0.;
    towardTargetReward_ = 0.;
    stayTargetReward_ = 0.;
    commandReward_ = 0.;
    torqueReward_ = 0.;
    stayObjectHeadingReward_ = 0.;

    return float(stepData_.sum());
  }

  [[nodiscard]] bool isTerminalState(float &terminalReward) {
    terminalReward = float(terminalRewardCoeff_);

    /// if the contact body is not feet
    for (auto &contact: raibo_->getContacts())
      if (std::find(footIndices_.begin(), footIndices_.end(), contact.getlocalBodyIndex()) == footIndices_.end() || contact.isSelfCollision())
        return true;


    terminalReward = 0.f;
//    Eigen::Vector3d obj_to_target = (command_Obj_Pos_ - Obj_Pos_.e());
//    if (obj_to_target.norm() < 0.03)
//      terminalReward = 0.1;



    return false;
  }

  void updateObservation(bool nosify,
                         const Eigen::Vector3d &command,
                         const raisim::HeightMap *map,
                         std::mt19937 &gen_,
                         std::normal_distribution<double> &normDist_) {


    // update History
    for (int i=0; i< historyNum_; i++) {
      obDouble_.segment((exteroceptiveDim_ + proprioceptiveDim_ + actionDim_)*i,
                        proprioceptiveDim_) = stateInfoHistory_[i];
      obDouble_.segment((exteroceptiveDim_ + proprioceptiveDim_ + actionDim_)*i + proprioceptiveDim_,
                        exteroceptiveDim_) = objectInfoHistory_[i];
      obDouble_.segment((exteroceptiveDim_ + proprioceptiveDim_ + actionDim_)*i + proprioceptiveDim_ + exteroceptiveDim_,
                        actionDim_) = actionInfoHistory_[i+1];
    }

    // current state
    obDouble_.segment((exteroceptiveDim_ + proprioceptiveDim_ + actionDim_)*historyNum_, 3) = baseRot_.e().row(2);

//    /// body velocities
    obDouble_.segment((exteroceptiveDim_ + proprioceptiveDim_ + actionDim_)*historyNum_ + 3, 3) = bodyLinVel_;
    obDouble_.segment((exteroceptiveDim_ + proprioceptiveDim_ + actionDim_)*historyNum_ + 6, 3) = bodyAngVel_;

    obDouble_.segment((exteroceptiveDim_ + proprioceptiveDim_ + actionDim_)*historyNum_ + proprioceptiveDim_, exteroceptiveDim_) = Obj_Info_;

    obDouble_.segment((exteroceptiveDim_ + proprioceptiveDim_ + actionDim_)*historyNum_ + proprioceptiveDim_+exteroceptiveDim_, actionDim_) = actionInfoHistory_.back();


  }

  inline void checkConfig(const Yaml::Node &cfg) {
    READ_YAML(int, proprioceptiveDim_, cfg["dimension"]["proprioceptiveDim_"])
    READ_YAML(int, exteroceptiveDim_, cfg["dimension"]["exteroceptiveDim_"])
    READ_YAML(int, historyNum_, cfg["dimension"]["historyNum_"])
    READ_YAML(int, actionNum_, cfg["dimension"]["actionhistoryNum_"])
  }

  inline void setRewardConfig(const Yaml::Node &cfg) {
    READ_YAML(double, towardObjectRewardCoeff_, cfg["reward"]["towardObjectRewardCoeff_"])
    READ_YAML(double, stayObjectRewardCoeff_, cfg["reward"]["stayObjectRewardCoeff_"])
    READ_YAML(double, towardTargetRewardCoeff_, cfg["reward"]["towardTargetRewardCoeff_"])
    READ_YAML(double, stayTargetRewardCoeff_, cfg["reward"]["stayTargetRewardCoeff_"])
    READ_YAML(double, commandRewardCoeff_, cfg["reward"]["commandRewardCoeff_"])
    READ_YAML(double, torqueRewardCoeff_, cfg["reward"]["torque_reward_coeff"])
    READ_YAML(double, stayObjectHeadingRewardCoeff_, cfg["reward"]["stayObjectHeadingRewardCoeff_"])
  }

  void updateObject(raisim::SingleBodyObject* obj) {
    Obj_ = obj;
  }

  void updateClassifyvector(Eigen::VectorXd &classify) {
    classify_vector_ = classify;
  }

  inline void accumulateRewards(double cf, const Eigen::Vector3d &cm) {
    /// move towards the object

    Eigen::Vector3d ee_to_obj = (Obj_Pos_.e()-ee_Pos_w_.e());
    Eigen::Vector3d obj_to_target (command_Obj_Pos_ - Obj_Pos_.e());
    Eigen::Vector3d ee_to_target = (command_Obj_Pos_ - Obj_Pos_.e());
    ee_to_obj(2) = 0;
    obj_to_target(2) = 0;
    ee_to_target(2) = 0;
    Obj_Vel_(2) = 0;
//    ee_to_obj = baseRot_.e().transpose() * ee_to_obj;
//    obj_to_target = baseRot_.e().transpose() * obj_to_target;
//    ee_to_target = baseRot_.e().transpose() * ee_to_target;

    double toward_o = (ee_to_obj * (1. / (ee_to_obj.norm() + 1e-8))).transpose()*(ee_Vel_w_.e() * (1. / (ee_Vel_w_.e().norm() + 1e-8))) - 1;
    towardObjectReward_ += cf * towardObjectRewardCoeff_ * simDt_ * exp(-std::pow(std::min(0.0, toward_o), 2));

    Eigen::Vector3d heading; heading << baseRot_[0], baseRot_[1], 0;

    /// stay close to the object
    double stay_o = ee_to_obj.norm(); /// max : inf, min : 0
    double stay_o_heading = Obj_Vel_.e().dot(heading) / (heading.norm() * Obj_Vel_.e().norm() + 1e-8) - 1; /// max : 1, min : 0
    stayObjectReward_ += cf * stayObjectRewardCoeff_ * simDt_ * exp(-stay_o);
    stayObjectHeadingReward_ += cf * stayObjectHeadingRewardCoeff_ * simDt_ * exp(stay_o_heading);

    /// move the object towards the target
    double toward_t = (obj_to_target * (1. / (obj_to_target.norm() + 1e-8))).transpose()*(Obj_Vel_.e() * (1./ (Obj_Vel_.e().norm() + 1e-8))) - 1;
    towardTargetReward_ += cf * towardTargetRewardCoeff_ * simDt_ * exp(-std::pow(std::min(0.0, toward_t), 2));

    /// keep the object close to the target
    double stay_t = obj_to_target.norm();
    stayTargetReward_ += cf * stayTargetRewardCoeff_ * simDt_ * exp(-stay_t);

    double commandReward_tmp = std::max(5., static_cast<double>(command_.norm()));
    commandReward_ += cf * commandRewardCoeff_ * simDt_ * commandReward_tmp;

    torqueReward_ += cf * torqueRewardCoeff_ * simDt_ * raibo_->getGeneralizedForce().norm();

  }

  inline void setStandingMode(bool mode) { standingMode_ = mode; }

  [[nodiscard]] const Eigen::VectorXd &getJointPositionHistory() const { return jointPositionHistory_; }
  [[nodiscard]] const Eigen::VectorXd &getJointVelocityHistory() const { return jointVelocityHistory_; }

  [[nodiscard]] static constexpr int getObDim() { return obDim_; }
  [[nodiscard]] static constexpr int getActionDim() { return actionDim_; }
  [[nodiscard]] static constexpr double getSimDt() { return simDt_; }
  [[nodiscard]] static constexpr double getConDt() { return conDt_; }
  void getState(Eigen::Ref<EigenVec> gc, Eigen::Ref<EigenVec> gv) { gc = gc_.cast<float>(); gv = gv_.cast<float>(); }

  static void setSimDt(double dt) {
    RSFATAL_IF(fabs(dt - simDt_) > 1e-12, "sim dt is fixed to " << simDt_)
  };
  static void setConDt(double dt) {
    RSFATAL_IF(fabs(dt - conDt_) > 1e-12, "con dt is fixed to " << conDt_)};

  [[nodiscard]] inline const std::vector<std::string> &getStepDataTag() const { return stepDataTag_; }
  [[nodiscard]] inline const Eigen::VectorXd &getStepData() const { return stepData_; }

  // robot configuration variables
  raisim::ArticulatedSystem *raibo_;
  std::vector<size_t> footIndices_, footFrameIndicies_, armIndices_;
  Eigen::VectorXd nominalConfig_;
  static constexpr int actionDim_ = 2; /// output dim : joint action 12 + task space action 6 + gain dim 4
  static constexpr size_t historyLength_ = 14;

  int proprioceptiveDim_ = 9;
  int exteroceptiveDim_ = 38;
  int historyNum_ = 4;
  int actionNum_ = 5;

  static constexpr size_t obDim_ = 245;

//  static constexpr size_t obDim_ = (proprioceptiveDim_ + exteroceptiveDim_) * (historyNum_+1) +  actionDim_ * actionNum_;

  static constexpr double simDt_ = .001;
  static constexpr int gcDim_ = 19;
  static constexpr int gvDim_ = 18;
  static constexpr int nPosHist_ = 3;
  static constexpr int nVelHist_ = 4;
  raisim::SingleBodyObject* Obj_;
  static constexpr int nJoints_ = 12;
  static constexpr int is_foot_contact_ = 0;

  // robot state variables
  Eigen::VectorXd gc_, gv_;
  Eigen::Vector3d bodyLinVel_, bodyAngVel_; /// body velocities are expressed in the body frame
  Eigen::VectorXd jointVelocity_;
  std::array<raisim::Vec<3>, 4> footPos_, footVel_;
  raisim::Vec<3> zAxis_ = {0., 0., 1.}, controlFrameX_, controlFrameY_;
  Eigen::VectorXd jointPositionHistory_;
  Eigen::VectorXd jointVelocityHistory_;
  Eigen::VectorXd historyTempMemory_;
  std::vector<Eigen::VectorXd> objectInfoHistory_;
  std::vector<Eigen::VectorXd> stateInfoHistory_;
  std::vector<Eigen::VectorXd> actionInfoHistory_;
  Eigen::VectorXd historyTempMemory_2;
  std::array<bool, 4> footContactState_;
  raisim::Mat<3, 3> baseRot_;
  Eigen::Vector3f command_;

  // robot observation variables
  std::vector<raisim::VecDyn> heightScan_;
  Eigen::VectorXi scanConfig_;
  Eigen::VectorXd obDouble_, obMean_, obStd_;
  std::vector<std::vector<raisim::Vec<2>>> scanPoint_;
  Eigen::MatrixXd scanSin_;
  Eigen::MatrixXd scanCos_;
  Eigen::VectorXd Obj_Info_;
  raisim::Vec<3> Obj_Pos_, Obj_Vel_, Obj_AVel_;
  raisim::Mat<3,3> Obj_Rot_, Tar_Rot_;
  raisim::Vec<3> ee_Pos_w_, ee_Vel_w_, ee_Avel_w_;
  raisim::Mat<3,3> eeRot_w_;

  // control variables
  static constexpr double conDt_ = 0.25;
  bool standingMode_ = false;
  Eigen::VectorXd actionMean_, actionStd_, actionScaled_;
  Eigen::VectorXd actionTarget_;
  Eigen::Vector3d command_Obj_Pos_;
  Eigen::Vector3d obj_geometry_;
  Eigen::VectorXd classify_vector_;


  // reward variables
  double towardObjectRewardCoeff_ = 0., towardObjectReward_ = 0.;
  double stayObjectRewardCoeff_ = 0., stayObjectReward_ = 0.;
  double towardTargetRewardCoeff_ = 0., towardTargetReward_ = 0.;
  double stayTargetRewardCoeff_ = 0., stayTargetReward_ = 0.;
  double terminalRewardCoeff_ = 0.0;
  double commandRewardCoeff_ = 0., commandReward_ = 0.;
  double torqueRewardCoeff_ = 0., torqueReward_ = 0.;
  double stayObjectHeadingReward_ = 0., stayObjectHeadingRewardCoeff_ = 0.;

  // exported data
  Eigen::VectorXd stepData_;
  std::vector<std::string> stepDataTag_;
};

}

#endif //_RAISIM_GYM_RAIBO_CONTROLLER_HPP