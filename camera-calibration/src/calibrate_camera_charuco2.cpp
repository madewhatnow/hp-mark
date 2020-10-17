/*
By downloading, copying, installing or using the software you agree to this
license. If you do not agree to this license, do not download, install,
copy or use the software.

                          License Agreement
               For Open Source Computer Vision Library
                       (3-clause BSD License)

Copyright (C) 2013, OpenCV Foundation, all rights reserved.
Third party copyrights are property of their respective owners.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  * Neither the names of the copyright holders nor the names of the contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.

This software is provided by the copyright holders and contributors "as is" and
any express or implied warranties, including, but not limited to, the implied
warranties of merchantability and fitness for a particular purpose are
disclaimed. In no event shall copyright holders or contributors be liable for
any direct, indirect, incidental, special, exemplary, or consequential damages
(including, but not limited to, procurement of substitute goods or services;
loss of use, data, or profits; or business interruption) however caused
and on any theory of liability, whether in contract, strict liability,
or tort (including negligence or otherwise) arising in any way out of
the use of this software, even if advised of the possibility of such damage.
*/

#include <ctime>
#include <iostream>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>

using namespace std;
using namespace cv;

namespace {
const char* about =
        "Calibration using a ChArUco board\n"
        "  To capture a frame for calibration, press 'c',\n"
        "  If input comes from video, press any key for next frame\n"
        "  To finish capturing, press 'ESC' key and calibration starts.\n";
const char *keys =
    "{w        |       | Number of squares in X direction }"
    "{h        |       | Number of squares in Y direction }"
    "{sl       |       | Square side length (in meters) }"
    "{ml       |       | Marker side length (in meters) }"
    "{d        |       | dictionary: DICT_4X4_50=0, DICT_4X4_100=1, "
    "DICT_4X4_250=2,"
    "DICT_4X4_1000=3, DICT_5X5_50=4, DICT_5X5_100=5, DICT_5X5_250=6, "
    "DICT_5X5_1000=7, "
    "DICT_6X6_50=8, DICT_6X6_100=9, DICT_6X6_250=10, DICT_6X6_1000=11, "
    "DICT_7X7_50=12,"
    "DICT_7X7_100=13, DICT_7X7_250=14, DICT_7X7_1000=15, DICT_ARUCO_ORIGINAL = "
    "16}"
    "{@outfile |<none> | Output file with calibrated camera parameters }"
    "{v        |       | Input from video file, if ommited, input comes from "
    "camera }"
    "{l        |       | List of input images }"
    "{ci       | 0     | Camera id if input doesnt come from video (-v) }"
    "{dp       |       | File of marker detector parameters }"
    "{rs       | false | Apply refind strategy }"
    "{zt       | false | Assume zero tangential distortion }"
    "{a        |       | Fix aspect ratio (fx/fy) to this value }"
    "{pc       | false | Fix the principal point at the center }"
    "{test     | false | For an image sequence, show what got detected, don't "
    "calculate anything }"
    "{sc       | false | Show detected chessboard corners after calibration }";
}

static bool readStringList(const string &filename, vector<string> &l) {
  l.resize(0);
  FileStorage fs(filename, FileStorage::READ);
  if (!fs.isOpened())
    return false;
  size_t dir_pos = filename.rfind('/');
  if (dir_pos == string::npos)
    dir_pos = filename.rfind('\\');
  FileNode n = fs.getFirstTopLevelNode();
  if (n.type() != FileNode::SEQ)
    return false;
  FileNodeIterator it = n.begin(), it_end = n.end();
  for (; it != it_end; ++it) {
    string fname = (string)*it;
    if (dir_pos != string::npos) {
      string fpath =
          samples::findFile(filename.substr(0, dir_pos + 1) + fname, false);
      if (fpath.empty()) {
        fpath = samples::findFile(fname);
      }
      fname = fpath;
    } else {
      fname = samples::findFile(fname);
    }
    l.push_back(fname);
  }
  return true;
}

/**
 */
static bool readDetectorParameters(string filename, Ptr<aruco::DetectorParameters> &params) {
    FileStorage fs(filename, FileStorage::READ);
    if(!fs.isOpened())
        return false;
    fs["adaptiveThreshWinSizeMin"] >> params->adaptiveThreshWinSizeMin;
    fs["adaptiveThreshWinSizeMax"] >> params->adaptiveThreshWinSizeMax;
    fs["adaptiveThreshWinSizeStep"] >> params->adaptiveThreshWinSizeStep;
    fs["adaptiveThreshConstant"] >> params->adaptiveThreshConstant;
    fs["minMarkerPerimeterRate"] >> params->minMarkerPerimeterRate;
    fs["maxMarkerPerimeterRate"] >> params->maxMarkerPerimeterRate;
    fs["polygonalApproxAccuracyRate"] >> params->polygonalApproxAccuracyRate;
    fs["minCornerDistanceRate"] >> params->minCornerDistanceRate;
    fs["minDistanceToBorder"] >> params->minDistanceToBorder;
    fs["minMarkerDistanceRate"] >> params->minMarkerDistanceRate;
    fs["cornerRefinementMethod"] >> params->cornerRefinementMethod;
    fs["cornerRefinementWinSize"] >> params->cornerRefinementWinSize;
    fs["cornerRefinementMaxIterations"] >> params->cornerRefinementMaxIterations;
    fs["cornerRefinementMinAccuracy"] >> params->cornerRefinementMinAccuracy;
    fs["markerBorderBits"] >> params->markerBorderBits;
    fs["perspectiveRemovePixelPerCell"] >> params->perspectiveRemovePixelPerCell;
    fs["perspectiveRemoveIgnoredMarginPerCell"] >> params->perspectiveRemoveIgnoredMarginPerCell;
    fs["maxErroneousBitsInBorderRate"] >> params->maxErroneousBitsInBorderRate;
    fs["minOtsuStdDev"] >> params->minOtsuStdDev;
    fs["errorCorrectionRate"] >> params->errorCorrectionRate;
    return true;
}



/**
 */
static bool saveCameraParams(const string &filename, Size imageSize, float aspectRatio, int flags,
                             const Mat &cameraMatrix, const Mat &distCoeffs, double totalAvgErr) {
    FileStorage fs(filename, FileStorage::WRITE);
    if(!fs.isOpened())
        return false;

    time_t tt;
    time(&tt);
    struct tm *t2 = localtime(&tt);
    char buf[1024];
    strftime(buf, sizeof(buf) - 1, "%c", t2);

    fs << "calibration_time" << buf;

    fs << "image_width" << imageSize.width;
    fs << "image_height" << imageSize.height;

    if(flags & CALIB_FIX_ASPECT_RATIO) fs << "aspectRatio" << aspectRatio;

    if(flags != 0) {
        sprintf(buf, "flags: %s%s%s%s",
                flags & CALIB_USE_INTRINSIC_GUESS ? "+use_intrinsic_guess" : "",
                flags & CALIB_FIX_ASPECT_RATIO ? "+fix_aspectRatio" : "",
                flags & CALIB_FIX_PRINCIPAL_POINT ? "+fix_principal_point" : "",
                flags & CALIB_ZERO_TANGENT_DIST ? "+zero_tangent_dist" : "");
    }

    fs << "flags" << flags;

    fs << "camera_matrix" << cameraMatrix;
    fs << "distortion_coefficients" << distCoeffs;

    fs << "avg_reprojection_error" << totalAvgErr;

    return true;
}

double calibrateCameraCharuco2(InputArrayOfArrays _charucoCorners,
                               InputArrayOfArrays _charucoIds,
                               const Ptr<aruco::CharucoBoard> &_board,
                               Size imageSize, InputOutputArray _cameraMatrix,
                               InputOutputArray _distCoeffs,
                               OutputArrayOfArrays _rvecs,
                               OutputArrayOfArrays _tvecs, int flags,
                               int iFix) {

  CV_Assert(_charucoIds.total() > 0 &&
            (_charucoIds.total() == _charucoCorners.total()));

  // Join object points of charuco corners in a single vector for
  // calibrateCamera() function
  vector<vector<Point3f>> allObjPoints;
  allObjPoints.resize(_charucoIds.total());
  for (unsigned int i = 0; i < _charucoIds.total(); i++) {
    unsigned int nCorners = (unsigned int)_charucoIds.getMat(i).total();
    CV_Assert(nCorners > 0 && nCorners == _charucoCorners.getMat(i).total());
    allObjPoints[i].reserve(nCorners);

    for (unsigned int j = 0; j < nCorners; j++) {
      int pointId = _charucoIds.getMat(i).at<int>(j);
      CV_Assert(pointId >= 0 &&
                pointId < (int)_board->chessboardCorners.size());
      allObjPoints[i].push_back(_board->chessboardCorners[pointId]);
    }
  }

  // Code to help us find the top right corner...
  // for (int j{0}; j < allObjPoints[0].size(); ++j) {
  //  cout << j << " is (" << allObjPoints[0][j] << ") distances to 0th corner:
  //  "
  //       << allObjPoints[0][0].x - allObjPoints[0][j].x << ", "
  //       << allObjPoints[0][0].y - allObjPoints[0][j].y
  //       << " distances to last corner: "
  //       << allObjPoints[0][allObjPoints[0].size() - 1].x -
  //       allObjPoints[0][j].x
  //       << ", "
  //       << allObjPoints[0][allObjPoints[0].size() - 1].y -
  //       allObjPoints[0][j].y
  //       << " distance from both: "
  //       << cv::norm(allObjPoints[0][0] - allObjPoints[0][j]) +
  //              cv::norm(allObjPoints[0][allObjPoints[0].size() - 1] -
  //                       allObjPoints[0][j])
  //       << endl;
  //}
  for (auto const &point : allObjPoints) {
    cout << point.size() << endl;
  }
  return calibrateCameraRO(allObjPoints, _charucoCorners, imageSize, iFix,
                           _cameraMatrix, _distCoeffs, _rvecs, _tvecs,
                           noArray(), flags);
}

/**
 */
int main(int argc, char *argv[]) {
    CommandLineParser parser(argc, argv, keys);
    parser.about(about);

    if(argc < 7) {
        parser.printMessage();
        return 0;
    }

    int squaresX = parser.get<int>("w");
    int squaresY = parser.get<int>("h");
    float squareLength = parser.get<float>("sl");
    float markerLength = parser.get<float>("ml");
    int dictionaryId = parser.get<int>("d");
    string const imageListFilename = parser.get<string>("l");
    string outputFile = parser.get<string>(0);

    bool showChessboardCorners = parser.get<bool>("sc");
    bool isTestRun = parser.get<bool>("test");

    int calibrationFlags = 0;
    float aspectRatio = 1;
    if(parser.has("a")) {
        calibrationFlags |= CALIB_FIX_ASPECT_RATIO;
        aspectRatio = parser.get<float>("a");
    }
    if(parser.get<bool>("zt")) calibrationFlags |= CALIB_ZERO_TANGENT_DIST;
    if(parser.get<bool>("pc")) calibrationFlags |= CALIB_FIX_PRINCIPAL_POINT;

    Ptr<aruco::DetectorParameters> detectorParams = aruco::DetectorParameters::create();
    if(parser.has("dp")) {
        bool readOk = readDetectorParameters(parser.get<string>("dp"), detectorParams);
        if(!readOk) {
            cerr << "Invalid detector parameters file" << endl;
            return 0;
        }
    }

    bool refindStrategy = parser.get<bool>("rs");
    int camId = parser.get<int>("ci");
    String video;

    if(parser.has("v")) {
        video = parser.get<String>("v");
    }

    if(!parser.check()) {
        parser.printErrors();
        return 0;
    }

    VideoCapture inputVideo;
    int waitTime;
    if (!video.empty() and !imageListFilename.empty()) {
      cout << "Can't have both video and image list input\n";
      return 1;
    }
    if (video.empty() and imageListFilename.empty()) {
      cout << "Connecting to cam nr " << camId << endl;
      inputVideo.open(camId);
      waitTime = 10;
    }
    if (!video.empty()) {
      cout << "Reading from video file " << video << endl;
      inputVideo.open(video);
      waitTime = 0;
    }
    vector<string> imageList{};
    if (!imageListFilename.empty()) {
      cout << "Reading from image list " << imageListFilename << endl;
      readStringList(samples::findFile(imageListFilename), imageList);
    }

    Ptr<aruco::Dictionary> dictionary = aruco::getPredefinedDictionary(
        aruco::PREDEFINED_DICTIONARY_NAME(dictionaryId));

    // create charuco board object
    Ptr<aruco::CharucoBoard> charucoboard = aruco::CharucoBoard::create(
        squaresX, squaresY, squareLength, markerLength, dictionary);
    Ptr<aruco::Board> board = charucoboard.staticCast<aruco::Board>();

    // collect data from each frame
    vector< vector< vector< Point2f > > > allCorners;
    vector< vector< int > > allIds;
    vector< Mat > allImgs;
    Size imgSize;

    // Done with initializations stuff

    if (imageList.empty()) {
      while (inputVideo.grab()) {
        Mat image, imageCopy;
        inputVideo.retrieve(image);

        vector<int> ids;
        vector<vector<Point2f>> corners, rejected;

        // detect markers
        aruco::detectMarkers(image, dictionary, corners, ids, detectorParams,
                             rejected);

        // refind strategy to detect more markers
        if (refindStrategy)
          aruco::refineDetectedMarkers(image, board, corners, ids, rejected);

        // interpolate charuco corners
        Mat currentCharucoCorners, currentCharucoIds;
        if (ids.size() > 0)
          aruco::interpolateCornersCharuco(corners, ids, image, charucoboard,
                                           currentCharucoCorners,
                                           currentCharucoIds);

        // draw results
        image.copyTo(imageCopy);
        if (ids.size() > 0)
          aruco::drawDetectedMarkers(imageCopy, corners);

        if (currentCharucoCorners.total() > 0)
          aruco::drawDetectedCornersCharuco(imageCopy, currentCharucoCorners,
                                            currentCharucoIds);

        putText(imageCopy,
                "Press 'c' to add current frame. 'ESC' to finish and calibrate",
                Point(10, 20), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 0, 0), 2);

        imshow("out", imageCopy);
        char key = (char)waitKey(waitTime);
        if (key == 27)
          break;
        if (key == 'c' && ids.size() > 0) {
          cout << "Frame captured" << endl;
          allCorners.push_back(corners);
          allIds.push_back(ids);
          allImgs.push_back(image);
          imgSize = image.size();
        }
      }
    } else {
      for (auto const &imageName : imageList) {
        cout << "Using image " << imageName;
        Mat image = imread(imageName, 1);
        vector<int> ids;
        vector<vector<Point2f>> corners, rejected;
        // detect markers
        aruco::detectMarkers(image, dictionary, corners, ids, detectorParams,
                             rejected);
        // refind strategy to detect more markers
        if (refindStrategy)
          aruco::refineDetectedMarkers(image, board, corners, ids, rejected);
        // interpolate charuco corners
        Mat currentCharucoCorners, currentCharucoIds;
        if (ids.size() > 0)
          aruco::interpolateCornersCharuco(corners, ids, image, charucoboard,
                                           currentCharucoCorners,
                                           currentCharucoIds);
        cout << " found " << corners.size() << " aruco tags" << endl;

        if (isTestRun) {
          // draw results
          Mat imageCopy;
          image.copyTo(imageCopy);
          if (ids.size() > 0)
            aruco::drawDetectedMarkers(imageCopy, corners);
          if (currentCharucoCorners.total() > 0)
            aruco::drawDetectedCornersCharuco(imageCopy, currentCharucoCorners,
                                              currentCharucoIds);
          Size const size{1280, 960};
          resize(imageCopy, imageCopy, size);
          putText(imageCopy, imageName, Point(10, 30), FONT_HERSHEY_SIMPLEX,
                  1.0, Scalar(255, 0, 0), 2);
          putText(imageCopy, "Did your aruco markers get detected?",
                  Point(10, 65), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(255, 0, 0),
                  2);
          putText(imageCopy, "Press any key to go to next image.",
                  Point(10, 100), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(255, 0, 0),
                  2);
          putText(imageCopy, "Press ESC to stop this test", Point(10, 135),
                  FONT_HERSHEY_SIMPLEX, 1.0, Scalar(255, 0, 0), 2);
          imshow("out", imageCopy);
          char key = (char)waitKey(0);
          if (key == 27)
            break;
        }

        if (!isTestRun) {
          allCorners.push_back(corners);
          allIds.push_back(ids);
          allImgs.push_back(image);
          imgSize = image.size();
        }
      }
    }
    if (isTestRun) {
      cout << "Test run finished\n";
      return 0;
    }

    if(allIds.size() < 1) {
      cerr << "Not enough captures for calibration" << endl;
      return 0;
    }

    Mat cameraMatrix, distCoeffs;
    vector< Mat > rvecs, tvecs;
    double repError;

    if(calibrationFlags & CALIB_FIX_ASPECT_RATIO) {
      cameraMatrix = Mat::eye(3, 3, CV_64F);
      cameraMatrix.at<double>(0, 0) = aspectRatio;
      cameraMatrix.at<double>(1, 1) = aspectRatio;
    }

    // prepare data for calibration
    vector< vector< Point2f > > allCornersConcatenated;
    vector< int > allIdsConcatenated;
    vector< int > markerCounterPerFrame;
    markerCounterPerFrame.reserve(allCorners.size());
    for(unsigned int i = 0; i < allCorners.size(); i++) {
      markerCounterPerFrame.push_back((int)allCorners[i].size());
      for (unsigned int j = 0; j < allCorners[i].size(); j++) {
        allCornersConcatenated.push_back(allCorners[i][j]);
        allIdsConcatenated.push_back(allIds[i][j]);
      }
    }

    // calibrate camera using aruco markers
    double arucoRepErr;
    arucoRepErr = aruco::calibrateCameraAruco(
        allCornersConcatenated, allIdsConcatenated, markerCounterPerFrame,
        board, imgSize, cameraMatrix, distCoeffs, noArray(), noArray(),
        calibrationFlags);

    // prepare data for charuco calibration
    int nFrames = (int)allCorners.size();
    vector< Mat > allCharucoCorners;
    vector< Mat > allCharucoIds;
    vector< Mat > filteredImages;
    allCharucoCorners.reserve(nFrames);
    allCharucoIds.reserve(nFrames);

    for(int i = 0; i < nFrames; i++) {
      // interpolate using camera parameters
      Mat currentCharucoCorners, currentCharucoIds;
      aruco::interpolateCornersCharuco(
          allCorners[i], allIds[i], allImgs[i], charucoboard,
          currentCharucoCorners, currentCharucoIds, cameraMatrix, distCoeffs);

      allCharucoCorners.push_back(currentCharucoCorners);
      allCharucoIds.push_back(currentCharucoIds);
      filteredImages.push_back(allImgs[i]);
    }

    if(allCharucoCorners.size() < 4) {
      cerr << "Not enough corners for calibration" << endl;
      return 0;
    }

    // calibrate camera using charuco
    repError = calibrateCameraCharuco2(
        allCharucoCorners, allCharucoIds, charucoboard, imgSize, cameraMatrix,
        distCoeffs, rvecs, tvecs, calibrationFlags, squaresX - 2);

    bool saveOk =
        saveCameraParams(outputFile, imgSize, aspectRatio, calibrationFlags,
                         cameraMatrix, distCoeffs, repError);
    if(!saveOk) {
      cerr << "Cannot save output file" << endl;
      return 0;
    }

    cout << "Rep Error: " << repError << endl;
    cout << "Rep Error Aruco: " << arucoRepErr << endl;
    cout << "Calibration saved to " << outputFile << endl;

    // show interpolated charuco corners for debugging
    if(showChessboardCorners) {
      for (unsigned int frame = 0; frame < filteredImages.size(); frame++) {
        Mat imageCopy = filteredImages[frame].clone();
        if (allIds[frame].size() > 0) {

          if (allCharucoCorners[frame].total() > 0) {
            aruco::drawDetectedCornersCharuco(
                imageCopy, allCharucoCorners[frame], allCharucoIds[frame]);
          }
        }

        Size const size{1280, 960};
        resize(imageCopy, imageCopy, size);
        putText(imageCopy, "Did the edges get straight?", Point(10, 65),
                FONT_HERSHEY_SIMPLEX, 1.0, Scalar(255, 0, 0), 2);
        putText(imageCopy, "Press any key to go to next image.", Point(10, 100),
                FONT_HERSHEY_SIMPLEX, 1.0, Scalar(255, 0, 0), 2);
        putText(imageCopy, "Press ESC to exit", Point(10, 135),
                FONT_HERSHEY_SIMPLEX, 1.0, Scalar(255, 0, 0), 2);
        imshow("out", imageCopy);
        char key = (char)waitKey(0);
        if (key == 27)
          break;
      }
    }

    return 0;
}