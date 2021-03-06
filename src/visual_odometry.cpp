// visual odometry, combining ideas from a number of different visual odometry
// algorithms
//
//   Andrew Howard, "Real-Time Stereo Visual Odometry for Autonomous Ground Vehicles,"
//   International Conference on Robots and Systems (IROS), September 2008
//
//   David Nister
//
//   Sibley et al.
//
//   TODO
//
// Modifications include:
//  - multi-resolution odometry using gaussian pyramids
//
//  - adaptive feature detector thresholding
//
//  - subpixel refinement of feature matches
//
//  TODO

#ifdef WIN32
#define snprintf _snprintf_s
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "tictoc.hpp"
#include "visual_odometry.hpp"
#include "initial_homography_estimation.hpp"
#include "internal_utils.hpp"

#include <iostream>
#include <iomanip>

//#define dbg(...) fprintf(stderr, __VA_ARGS__)
#define dbg(...)
#define dump(var) (std::cerr<<" "#var<<" =[\n"<< std::setprecision (12)<<var<<"]"<<std::endl)

#ifndef MIN
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#endif

namespace fovis
{

static inline int clamp(int val, int minval, int maxval)
{
  if(val > maxval) return maxval;
  if(val < minval) return minval;
  return val;
}

static void validateOptions(const VisualOdometryOptions& options,
    const VisualOdometryOptions& defaults);

// =================== VisualOdometry ===================

VisualOdometry::VisualOdometry(const CameraIntrinsicsParameters& params, const VisualOdometryOptions& options) :
    _options(options), _params(params)
{

	_clearfp();
	_controlfp(_controlfp(0, 0) & ~(_EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW),
		_MCW_EM);

  _ref_frame = NULL;
  _prev_frame = NULL;
  _cur_frame = NULL;

  _change_reference_frames = false;

  _ref_frame_change_threshold = 150;

  _frame_count = 0;

  // check for any unrecognized options
  VisualOdometryOptions defaults;
  getDefaultOptions(&defaults);
  validateOptions(options, defaults);

  // extract options
  _feature_window_size = optionsGetIntOrFromDefault(_options, "feature-window-size", defaults);
  _num_pyramid_levels = optionsGetIntOrFromDefault(_options, "max-pyramid-level", defaults);
  _target_pixels_per_feature = optionsGetIntOrFromDefault(_options, "target-pixels-per-feature", defaults);
  _ref_frame_change_threshold = optionsGetIntOrFromDefault(_options, "ref-frame-change-threshold", defaults);
  _use_homography_initialization = optionsGetBoolOrFromDefault(_options, "use-homography-initialization", defaults);
  _fast_threshold = optionsGetIntOrFromDefault(_options, "fast-threshold", defaults);
  _use_adaptive_threshold = optionsGetBoolOrFromDefault(_options, "use-adaptive-threshold", defaults);
  _fast_threshold_adaptive_gain = optionsGetDoubleOrFromDefault(_options, "fast-threshold-adaptive-gain", defaults);

  _fast_threshold_min = 5;
  _fast_threshold_max = 70;

  _p = new VisualOdometryPriv();

  _p->motion_estimate.setIdentity();
  _p->motion_estimate_covariance.setIdentity(6, 6);
  _p->pose.setIdentity();

  _p->ref_to_prev_frame.setIdentity();

  _ref_frame = new OdometryFrame(_params, options);

  _prev_frame = new OdometryFrame(_params, options);

  _cur_frame = new OdometryFrame(_params, options);

  _estimator = new MotionEstimator(_params, _options);
}

VisualOdometry::~VisualOdometry()
{
  delete _estimator;
  delete _ref_frame;
  delete _prev_frame;
  delete _cur_frame;
  delete _p;
  _ref_frame = NULL;
  _prev_frame = NULL;
  _cur_frame = NULL;
  tictoc_print_stats(TICTOC_AVG);
}

// Estimate an initial rotation by finding the 2D homography that best aligns
// a template image (the previous image) with the current image.  From this
// homography, extract initial rotation parameters.
Eigen::Quaterniond
VisualOdometry::estimateInitialRotation(const OdometryFrame* prev, const OdometryFrame* cur,
    const Eigen::Isometry3d &init_motion_estimate)
{
  _initial_rotation_pyramid_level = 4;
  int num_pyr_levels = prev->getNumLevels();
  int pyrLevel = MIN(num_pyr_levels-1,_initial_rotation_pyramid_level);
  const PyramidLevel * ref_level = prev->getLevel(pyrLevel);
  const PyramidLevel * target_level = cur->getLevel(pyrLevel);

  InitialHomographyEstimator rotation_estimator;
  rotation_estimator.setTemplateImage(ref_level->getGrayscaleImage(),
      ref_level->getWidth(), ref_level->getHeight(),
      ref_level->getGrayscaleImageStride(),
      _initial_rotation_pyramid_level - pyrLevel);

  rotation_estimator.setTestImage(target_level->getGrayscaleImage(),
      target_level->getWidth(), target_level->getHeight(),
      target_level->getGrayscaleImageStride(),
      _initial_rotation_pyramid_level - pyrLevel);

  Eigen::Matrix3f H = Eigen::Matrix3f::Identity();
  double finalRMS = 0;
  H = rotation_estimator.track(H,8, &finalRMS);
  double scale_factor = 1 << _initial_rotation_pyramid_level;
  Eigen::Matrix3f S = Eigen::Matrix3f::Identity() * scale_factor;
  S(2, 2) = 1;
  //scale H up to the full size image
  H = S * H * S.inverse();
  _p->initial_homography_est = H.cast<double>();
  //    dump(H);

  //TODO: use a better method to get the rotation from homography.
  Eigen::Vector3d rpy = Eigen::Vector3d::Zero();
  rpy(0) = asin(H(1, 2) / _params.fx);
  rpy(1) = -asin(H(0, 2) / _params.fx);
  rpy(2) = -atan2(H(1, 0), H(0, 0));

  //    Eigen::Vector3d rpy_deg = rpy.transpose() * 180 / M_PI;
  //    dbg("irot:(% 6.3f % 6.3f % 6.3f)",rpy_deg(0),rpy_deg(1),rpy_deg(2));

  Eigen::Quaterniond q = _rpy_to_quat(rpy);
  return q;
}

void
VisualOdometry::processFrame(const uint8_t* gray, DepthSource* depth_source)
{
  if(_change_reference_frames) {
    // current frame becomes the reference frame
    std::swap(_ref_frame, _cur_frame);
    _p->ref_to_prev_frame.setIdentity();
  } else {
    // reference frame doesn't change, current frame is now previous
    std::swap(_prev_frame, _cur_frame);
  }

  bool changed_reference_frames = _change_reference_frames;

  // initialize a null motion estimate
  _p->motion_estimate.setIdentity();
  _change_reference_frames = false;

  // detect features in new frame
  _cur_frame->prepareFrame(gray, _fast_threshold, depth_source);

  int width = _params.width;
  int height = _params.height;

  if (_use_adaptive_threshold) {
    // adaptively adjust feature detector threshold based on how many features
    // were detected.  Use proportional control
    int target_num_features = width * height / _target_pixels_per_feature;
    int err = _cur_frame->getNumDetectedKeypoints() - target_num_features;
    int thresh_adjustment = (int)((err) * _fast_threshold_adaptive_gain);
    _fast_threshold += thresh_adjustment;
    _fast_threshold = clamp(_fast_threshold, _fast_threshold_min, _fast_threshold_max);
  //  dbg("Next FAST threshold: %d (%d)\n", _fast_threshold, thresh_adjustment);
  }

  _frame_count++;

  // Only do the temporal matching if we have feature descriptors from the
  // previous frame.
  if(_frame_count < 2) {
    _change_reference_frames = true;
    return;
  }

  //TODO: should add option to pass in the initial estimate from external source
  tictoc("estimateInitialRotation");
  Eigen::Quaterniond init_rotation_est;
  if (_use_homography_initialization) {
    if (changed_reference_frames) {
      //TODO:this is ugly, but necessary due cuz of swapping stuff above :-/
      init_rotation_est = estimateInitialRotation(_ref_frame, _cur_frame);
    } else {
      init_rotation_est = estimateInitialRotation(_prev_frame, _cur_frame);
    }
  } else {
    init_rotation_est = Eigen::Quaterniond(1, 0, 0, 0); // identity quaternion.
  }

  tictoc("estimateInitialRotation");
  _p->initial_motion_estimate = _p->ref_to_prev_frame.inverse();
  _p->initial_motion_estimate.rotate(init_rotation_est);

  _p->initial_motion_cov.setIdentity();
  //TODO:estimate the covariance

  _estimator->estimateMotion(_ref_frame,
                             _cur_frame,
                             depth_source,
                             _p->initial_motion_estimate,
                             _p->initial_motion_cov);

  if(_estimator->isMotionEstimateValid()) {
    Eigen::Isometry3d to_reference = _estimator->getMotionEstimate();
    _p->motion_estimate = _p->ref_to_prev_frame * to_reference;
    Eigen::MatrixXd to_reference_cov = _estimator->getMotionEstimateCov();
    _p->motion_estimate_covariance = to_reference_cov; //TODO: this should probably be transformed as well
    _p->ref_to_prev_frame = to_reference.inverse();
    _p->pose = _p->pose * _p->motion_estimate;
  } else if(!changed_reference_frames) {
    // if the motion estimation failed, then try estimating against the
    // previous frame if it's not the reference frame.
    dbg("  Failed against reference frame, trying previous frame...\n");
    _p->initial_motion_estimate.setIdentity();
    _p->initial_motion_estimate.rotate(init_rotation_est);
    _p->initial_motion_cov.setIdentity();
    //TODO:covariance?
    _estimator->estimateMotion(_prev_frame,
                               _cur_frame,
                               depth_source,
                               _p->initial_motion_estimate,
                               _p->initial_motion_cov);

    if(_estimator->isMotionEstimateValid()) {
      dbg("   ok, matched against previous frame.\n");
      _p->motion_estimate = _estimator->getMotionEstimate();
      _p->motion_estimate_covariance = _estimator->getMotionEstimateCov();
      _p->pose = _p->pose * _p->motion_estimate;
      _change_reference_frames = true;
    }

  }

  // switch reference frames?
  if(!_estimator->isMotionEstimateValid() ||
      _estimator->getNumInliers() < _ref_frame_change_threshold) {
    _change_reference_frames = true;
  }
  if(_change_reference_frames)
    dbg("Changing reference frames\n");
}

void
VisualOdometry::sanityCheck() const
{
  _cur_frame->sanityCheck();
  _ref_frame->sanityCheck();
  _estimator->sanityCheck();
}

static std::string _toString(double v)
{
  char buf[80];
  snprintf(buf, sizeof(buf), "%f", v);
  return std::string(buf);
}

void VisualOdometry::getDefaultOptions(VisualOdometryOptions* d_options) {
	d_options->clear();

  //TODO split defaults?

  // VisualOdometry, OdometryFrame
  (*d_options)["feature-window-size"] = "9";
  (*d_options)["max-pyramid-level"] = "3";
  (*d_options)["min-pyramid-level"] = "0";
  (*d_options)["target-pixels-per-feature"] = "250";
  (*d_options)["fast-threshold"] = "20";
  (*d_options)["use-adaptive-threshold"] = "true";
  (*d_options)["fast-threshold-adaptive-gain"] = _toString(0.005);
  (*d_options)["use-homography-initialization"] = "true";
  (*d_options)["ref-frame-change-threshold"] = "150";

  // OdometryFrame
  (*d_options)["use-bucketing"] = "true";
  (*d_options)["bucket-width"] = "80";
  (*d_options)["bucket-height"] = "80";
  (*d_options)["max-keypoints-per-bucket"] = "25";
  (*d_options)["use-image-normalization"] = "false";

  // MotionEstimator
  (*d_options)["inlier-max-reprojection-error"] = _toString(1.5);
  (*d_options)["clique-inlier-threshold"] = _toString(0.1);
  (*d_options)["min-features-for-estimate"] = "10";
  (*d_options)["max-mean-reprojection-error"] = _toString(10.0);
  (*d_options)["use-subpixel-refinement"] = "true";
  (*d_options)["feature-search-window"] = "25";
  (*d_options)["update-target-features-with-refined"] = "false";

  // StereoDepth
  (*d_options)["stereo-require-mutual-match"] = "true";
  (*d_options)["stereo-max-dist-epipolar-line"] = _toString(1.5);
  (*d_options)["stereo-max-refinement-displacement"] = _toString(1.0);
  (*d_options)["stereo-max-disparity"] = "128";
}

static void
validateOptions(const VisualOdometryOptions& options,
    const VisualOdometryOptions& defaults)
{
  VisualOdometryOptions::const_iterator oiter = options.begin();
  VisualOdometryOptions::const_iterator oend = options.end();
  for(; oiter != oend; ++oiter) {
    if(defaults.find(oiter->first) == defaults.end()) {
      fprintf(stderr, "VisualOdometry WARNING: unrecognized option [%s]\n", oiter->first.c_str());
    }
  }

  // TODO check option value ranges
}


}
