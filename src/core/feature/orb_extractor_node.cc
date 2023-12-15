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

#include "core/feature/orb_extractor_node.h"

namespace PLSLAM
{
    namespace feature
    {

        std::array<orb_extractor_node, 4> orb_extractor_node::divide_node()
        {
            // Half width/height of the allocated patch area
            const unsigned int half_x = cvCeil((pt_end_.x - pt_begin_.x) / 2.0);
            const unsigned int half_y = cvCeil((pt_end_.y - pt_begin_.y) / 2.0);

            // Four new child nodes
            std::array<orb_extractor_node, 4> child_nodes;

            // A position of center top, left center, center, right center, and center bottom
            // These positions are used to determine new split areas
            const auto pt_top = cv::Point2i(pt_begin_.x + half_x, pt_begin_.y);
            const auto pt_left = cv::Point2i(pt_begin_.x, pt_begin_.y + half_y);
            const auto pt_center = cv::Point2i(pt_begin_.x + half_x, pt_begin_.y + half_y);
            const auto pt_right = cv::Point2i(pt_end_.x, pt_begin_.y + half_y);
            const auto pt_bottom = cv::Point2i(pt_begin_.x + half_x, pt_end_.y);

            // Assign new patch border for each child nodes
            child_nodes.at(0).pt_begin_ = pt_begin_;
            child_nodes.at(0).pt_end_ = pt_center;
            child_nodes.at(1).pt_begin_ = pt_top;
            child_nodes.at(1).pt_end_ = pt_right;
            child_nodes.at(2).pt_begin_ = pt_left;
            child_nodes.at(2).pt_end_ = pt_bottom;
            child_nodes.at(3).pt_begin_ = pt_center;
            child_nodes.at(3).pt_end_ = pt_end_;

            // Memory reservation for child nodes
            for (auto &node : child_nodes)
            {
                node.keypts_.reserve(keypts_.size());
            }

            // Distribute keypoints to child nodes
            for (const auto &keypt : keypts_)
            {
                unsigned int idx = 0;
                if (pt_begin_.x + half_x <= keypt.pt.x)
                {
                    idx += 1;
                }
                if (pt_begin_.y + half_y <= keypt.pt.y)
                {
                    idx += 2;
                }
                child_nodes.at(idx).keypts_.push_back(keypt);
            }

            return child_nodes;
        }

    } // namespace feature
} // namespace PLSLAM
