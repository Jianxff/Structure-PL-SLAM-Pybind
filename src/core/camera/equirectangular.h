/**
 * This file is part of Structure PLP-SLAM, originally from OpenVSLAM.
 *
 * Copyright 2022 DFKI (German Research Center for Artificial Intelligence)
 * Modified by Fangwen Shu <Fangwen.Shu@dfki.de>
 *
 * If you use this code, please cite the respective publications as
 * listed on the github repository.
 *
 * Structure PLP-SLAM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Structure PLP-SLAM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Structure PLP-SLAM. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PLSLAM_CAMERA_EQUIRECTANGULAR_H
#define PLSLAM_CAMERA_EQUIRECTANGULAR_H

#include "core/camera/base.h"

namespace PLSLAM
{
    namespace camera
    {

        class equirectangular final : public base
        {
        public:
            equirectangular(const std::string &name, const color_order_t &color_order,
                            const unsigned int cols, const unsigned int rows, const double fps);

            equirectangular(const YAML::Node &yaml_node);

            ~equirectangular() override;

            void show_parameters() const override final;

            image_bounds compute_image_bounds() const override final;

            cv::KeyPoint undistort_keypoint(const cv::KeyPoint &distorted) const override final
            {
                return distorted;
            }

            void undistort_keypoints(const std::vector<cv::KeyPoint> &dist_keypts, std::vector<cv::KeyPoint> &undist_keypts) const override final;

            Vec3_t convert_keypoint_to_bearing(const cv::KeyPoint &undist_keypt) const override final
            {
                // "From Google Street View to 3D City Models (ICCVW 2009)"
                Vec3_t bearing;

                // convert to unit polar coordinates
                const double longitude = (undist_keypt.pt.x / cols_ - 0.5) * (2.0 * M_PI);
                const double latitude = -(undist_keypt.pt.y / rows_ - 0.5) * M_PI;

                // convert to equirectangular coordinates
                bearing[0] = std::cos(latitude) * std::sin(longitude);
                bearing[1] = -std::sin(latitude);
                bearing[2] = std::cos(latitude) * std::cos(longitude);

                return bearing;
            }

            void convert_keypoints_to_bearings(const std::vector<cv::KeyPoint> &undist_keypts, eigen_alloc_vector<Vec3_t> &bearings) const override final;

            cv::KeyPoint convert_bearing_to_keypoint(const Vec3_t &bearing) const override final
            {
                cv::KeyPoint undistorted;

                // convert to unit polar coordinates
                const double latitude = -std::asin(bearing[1]);
                const double longitude = std::atan2(bearing[0], bearing[2]);

                // convert to pixel image coordinated
                undistorted.pt.x = cols_ * (0.5 + longitude / (2.0 * M_PI));
                undistorted.pt.y = rows_ * (0.5 - latitude / M_PI);

                return undistorted;
            }

            void convert_bearings_to_keypoints(const eigen_alloc_vector<Vec3_t> &bearings, std::vector<cv::KeyPoint> &undist_keypts) const override final;

            bool reproject_to_image(const Mat33_t &rot_cw, const Vec3_t &trans_cw, const Vec3_t &pos_w, Vec2_t &reproj, float &x_right) const override final;

            bool reproject_to_bearing(const Mat33_t &rot_cw, const Vec3_t &trans_cw, const Vec3_t &pos_w, Vec3_t &reproj) const override final;

            nlohmann::json to_json() const override final;
        };

    } // namespace camera
} // namespace PLSLAM

#endif // PLSLAM_CAMERA_EQUIRECTANGULAR_H
