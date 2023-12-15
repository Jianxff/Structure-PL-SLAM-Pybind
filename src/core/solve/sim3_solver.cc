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

#include "core/camera/base.h"
#include "core/data/keyframe.h"
#include "core/data/landmark.h"
#include "core/solve/sim3_solver.h"
#include "core/util/random_array.h"

#include <vector>
#include <cmath>

#include <opencv2/core/core.hpp>

namespace PLSLAM
{
    namespace solve
    {

        sim3_solver::sim3_solver(data::keyframe *keyfrm_1, data::keyframe *keyfrm_2,
                                 const std::vector<data::landmark *> &matched_lms_in_keyfrm_2,
                                 const bool fix_scale, const unsigned int min_num_inliers)
            : keyfrm_1_(keyfrm_1), keyfrm_2_(keyfrm_2),
              fix_scale_(fix_scale), min_num_inliers_(min_num_inliers)
        {
            // 3D point of keyframe1
            const auto keyfrm_1_lms = keyfrm_1_->get_landmarks();

            // Convert the 3D point to each camera reference
            const Mat33_t rot_1w = keyfrm_1_->get_rotation();
            const Vec3_t trans_1w = keyfrm_1_->get_translation();
            const Mat33_t rot_2w = keyfrm_2_->get_rotation();
            const Vec3_t trans_2w = keyfrm_2_->get_translation();

            // Allocate memory
            {
                const auto size = std::min(keyfrm_1_lms.size(), matched_lms_in_keyfrm_2.size());
                common_pts_in_keyfrm_1_.reserve(size);
                common_pts_in_keyfrm_2_.reserve(size);
                chi_sq_x_sigma_sq_1_.reserve(size);
                chi_sq_x_sigma_sq_2_.reserve(size);
                matched_indices_1_.reserve(size);
                matched_indices_2_.reserve(size);
            }

            // Chi-square value with 1% significance level (2 degrees of freedom)
            constexpr float chi_sq_2D = 9.21034;

            num_common_pts_ = 0;
            for (unsigned int idx1 = 0; idx1 < keyfrm_1_lms.size(); ++idx1)
            {
                if (!matched_lms_in_keyfrm_2.at(idx1))
                {
                    continue;
                }

                auto *lm_1 = keyfrm_1_lms.at(idx1);
                auto *lm_2 = matched_lms_in_keyfrm_2.at(idx1);

                if (!lm_1 || !lm_2)
                {
                    continue;
                }
                if (lm_1->will_be_erased() || lm_2->will_be_erased())
                {
                    continue;
                }

                int idx2 = lm_2->get_index_in_keyframe(keyfrm_2);

                if (idx2 < 0)
                {
                    continue;
                }

                const auto &keypt_1 = keyfrm_1_->undist_keypts_.at(idx1);
                const auto &keypt_2 = keyfrm_2_->undist_keypts_.at(idx2);

                const float sigma_sq_1 = keyfrm_1_->level_sigma_sq_.at(keypt_1.octave);
                const float sigma_sq_2 = keyfrm_2_->level_sigma_sq_.at(keypt_2.octave);

                chi_sq_x_sigma_sq_1_.push_back(chi_sq_2D * sigma_sq_1);
                chi_sq_x_sigma_sq_2_.push_back(chi_sq_2D * sigma_sq_2);

                matched_indices_1_.push_back(idx1);
                matched_indices_2_.push_back(idx2);

                const Vec3_t pos_w_1 = lm_1->get_pos_in_world();
                common_pts_in_keyfrm_1_.emplace_back(rot_1w * pos_w_1 + trans_1w);

                const Vec3_t pos_w_2 = lm_2->get_pos_in_world();
                common_pts_in_keyfrm_2_.emplace_back(rot_2w * pos_w_2 + trans_2w);

                ++num_common_pts_;
            }

            reproject_to_same_image(common_pts_in_keyfrm_1_, reprojected_1_, keyfrm_1_);
            reproject_to_same_image(common_pts_in_keyfrm_2_, reprojected_2_, keyfrm_2_);
        }

        void sim3_solver::find_via_ransac(const unsigned int max_num_iter)
        {
            // Initialize best model
            unsigned int max_num_inliers = 0;
            solution_is_valid_ = false;
            best_rot_12_ = Mat33_t::Zero();
            best_trans_12_ = Vec3_t::Zero();
            best_scale_12_ = 0.0;

            if (num_common_pts_ < 3 || num_common_pts_ < min_num_inliers_)
            {
                solution_is_valid_ = false;
                return;
            }

            // Variables used in the RANSAC loop
            Mat33_t rot_12_in_sac;
            Vec3_t trans_12_in_sac;
            float scale_12_in_sac;
            Mat33_t rot_21_in_sac;
            Vec3_t trans_21_in_sac;
            float scale_21_in_sac;

            // RANSAC loop
            for (unsigned int iter = 0; iter < max_num_iter; ++iter)
            {
                // Random sample 3D points to make a matrix
                Mat33_t pts_1, pts_2;
                const auto random_indices = util::create_random_array(3, 0, static_cast<int>(num_common_pts_ - 1));
                for (unsigned int i = 0; i < 3; ++i)
                {
                    pts_1.block(0, i, 3, 1) = common_pts_in_keyfrm_1_.at(random_indices.at(i));
                    pts_2.block(0, i, 3, 1) = common_pts_in_keyfrm_2_.at(random_indices.at(i));
                }

                // Find the similarity transformation matrix (R, t, s)
                compute_Sim3(pts_1, pts_2,
                             rot_12_in_sac, trans_12_in_sac, scale_12_in_sac,
                             rot_21_in_sac, trans_21_in_sac, scale_21_in_sac);

                // Count inliers
                std::vector<bool> inliers;
                const auto num_inliers = count_inliers(rot_12_in_sac, trans_12_in_sac, scale_12_in_sac,
                                                       rot_21_in_sac, trans_21_in_sac, scale_21_in_sac,
                                                       inliers);

                // Update best model
                if (max_num_inliers < num_inliers)
                {
                    max_num_inliers = num_inliers;
                    best_rot_12_ = rot_12_in_sac;
                    best_trans_12_ = trans_12_in_sac;
                    best_scale_12_ = scale_12_in_sac;
                }
            }

            if (max_num_inliers < min_num_inliers_)
            {
                // Estimation fails if the minimum requirement for the number of inliers cannot be met
                solution_is_valid_ = false;
                best_rot_12_ = Mat33_t::Zero();
                best_trans_12_ = Vec3_t::Zero();
                best_scale_12_ = 0.0;
                return;
            }
            else
            {
                solution_is_valid_ = true;
                return;
            }
        }

        void sim3_solver::compute_Sim3(const Mat33_t &pts_1, const Mat33_t &pts_2,
                                       Mat33_t &rot_12, Vec3_t &trans_12, float &scale_12,
                                       Mat33_t &rot_21, Vec3_t &trans_21, float &scale_21)
        {
            // Based on "Closed-form solution of absolute orientation using unit quaternions"
            // http://people.csail.mit.edu/bkph/papers/Absolute_Orientation.pdf

            // Find the centroid of each point set
            const Vec3_t centroid_1 = pts_1.rowwise().mean();
            const Vec3_t centroid_2 = pts_2.rowwise().mean();

            // Move the center of the distribution to centroid
            Mat33_t ave_pts_1 = pts_1;
            ave_pts_1.colwise() -= centroid_1;
            Mat33_t ave_pts_2 = pts_2;
            ave_pts_2.colwise() -= centroid_2;

            // 4.A Matrix of Sums of Products

            // Find the matrix M
            const Mat33_t M = ave_pts_1 * ave_pts_2.transpose();

            // Find the matrix N
            const double &Sxx = M(0, 0);
            const double &Syx = M(1, 0);
            const double &Szx = M(2, 0);
            const double &Sxy = M(0, 1);
            const double &Syy = M(1, 1);
            const double &Szy = M(2, 1);
            const double &Sxz = M(0, 2);
            const double &Syz = M(1, 2);
            const double &Szz = M(2, 2);
            Eigen::Matrix4d N;
            N << (Sxx + Syy + Szz), (Syz - Szy), (Szx - Sxz), (Sxy - Syx),
                (Syz - Szy), (Sxx - Syy - Szz), (Sxy + Syx), (Szx + Sxz),
                (Szx - Sxz), (Sxy + Syx), (-Sxx + Syy - Szz), (Syz + Szy),
                (Sxy - Syx), (Szx + Sxz), (Syz + Szy), (-Sxx - Syy + Szz);

            // 4.B Eigenvector Maximizes Matrix Product

            // Eigenvalue decomposition of N
            Eigen::EigenSolver<Mat44_t> eigensolver(N);

            // Find the maximum eigenvalue
            const auto &eigenvalues = eigensolver.eigenvalues();
            int max_idx = -1;
            double max_eigenvalue = -INFINITY;
            for (int idx = 0; idx < 4; ++idx)
            {
                if (max_eigenvalue <= eigenvalues(idx, 0).real())
                {
                    max_eigenvalue = eigenvalues(idx, 0).real();
                    max_idx = idx;
                }
            }
            const auto max_eigenvector = eigensolver.eigenvectors().col(max_idx);

            // Since it is a complex number, only the real number is extracted.
            Eigen::Vector4d eigenvector;
            eigenvector << max_eigenvector(0, 0).real(), max_eigenvector(1, 0).real(), max_eigenvector(2, 0).real(), max_eigenvector(3, 0).real();
            eigenvector.normalize();

            // make it a unit quaternion
            Eigen::Quaterniond q_rot_21(eigenvector(0), eigenvector(1), eigenvector(2), eigenvector(3));

            // Convert to rotation matrix
            rot_21 = q_rot_21.normalized().toRotationMatrix();

            // 2.D Finding the Scale

            if (fix_scale_)
            {
                scale_21 = 1.0;
            }
            else
            {
                // Convert averaged points 1 to points 2 coordinate system (rotation only)
                const Mat33_t ave_pts_1_in_2 = rot_21 * ave_pts_1;

                // denominator
                const double denom = ave_pts_1.squaredNorm();
                // numerator
                const double numer = ave_pts_2.cwiseProduct(ave_pts_1_in_2).sum();
                // Scale
                scale_21 = numer / denom;
            }

            // 2.C Centroids of the Sets of Measurements

            trans_21 = centroid_2 - scale_21 * rot_21 * centroid_1;

            // Reverse conversion
            rot_12 = rot_21.transpose();
            scale_12 = 1.0 / scale_21;
            trans_12 = -scale_12 * rot_12 * trans_21;
        }

        unsigned int sim3_solver::count_inliers(const Mat33_t &rot_12, const Vec3_t &trans_12, const float scale_12,
                                                const Mat33_t &rot_21, const Vec3_t &trans_21, const float scale_21,
                                                std::vector<bool> &inliers)
        {
            // Using the estimated similarity transformation matrix, reproject one 3D point onto the other image and calculate the distance.
            unsigned int num_inliers = 0;
            inliers.resize(num_common_pts_, false);

            // Project the 3D point of coordinate system 1 onto the image of coordinate system 2
            std::vector<Vec2_t, Eigen::aligned_allocator<Vec2_t>> reprojected_1_in_cam_2;
            reproject_to_other_image(common_pts_in_keyfrm_1_, reprojected_1_in_cam_2, rot_21, trans_21, scale_21, keyfrm_2_);

            // Project the 3D point of coordinate system 2 onto the image of coordinate system 1
            std::vector<Vec2_t, Eigen::aligned_allocator<Vec2_t>> reprojected_2_in_cam_1;
            reproject_to_other_image(common_pts_in_keyfrm_2_, reprojected_2_in_cam_1, rot_12, trans_12, scale_12, keyfrm_1_);

            for (unsigned int i = 0; i < num_common_pts_; ++i)
            {
                // Calculate the residual vector
                const Vec2_t dist_in_2 = (reprojected_1_in_cam_2.at(i) - reprojected_2_.at(i));
                const Vec2_t dist_in_1 = (reprojected_2_in_cam_1.at(i) - reprojected_1_.at(i));

                // Calculate squared error
                const double error_in_2 = dist_in_2.dot(dist_in_2);
                const double error_in_1 = dist_in_1.dot(dist_in_1);

                // Inlier check
                if (error_in_2 < chi_sq_x_sigma_sq_2_.at(i) && error_in_1 < chi_sq_x_sigma_sq_1_.at(i))
                {
                    inliers.at(i) = true;
                    ++num_inliers;
                }
            }

            return num_inliers;
        }

        void sim3_solver::reproject_to_other_image(const std::vector<Vec3_t, Eigen::aligned_allocator<Vec3_t>> &lm_coords_in_cam_1,
                                                   std::vector<Vec2_t, Eigen::aligned_allocator<Vec2_t>> &reprojected_in_cam_2,
                                                   const Mat33_t &rot_21, const Vec3_t &trans_21, const float scale_21, data::keyframe *keyfrm)
        {
            reprojected_in_cam_2.clear();
            reprojected_in_cam_2.reserve(lm_coords_in_cam_1.size());

            for (const auto &lm_coord_in_cam_1 : lm_coords_in_cam_1)
            {
                Vec2_t reproj_in_cam_2;
                float x_right;
                keyfrm->camera_->reproject_to_image(scale_21 * rot_21, trans_21, lm_coord_in_cam_1, reproj_in_cam_2, x_right);

                reprojected_in_cam_2.push_back(reproj_in_cam_2);
            }
        }

        void sim3_solver::reproject_to_same_image(const std::vector<Vec3_t, Eigen::aligned_allocator<Vec3_t>> &lm_coords_in_cam,
                                                  std::vector<Vec2_t, Eigen::aligned_allocator<Vec2_t>> &reprojected, data::keyframe *keyfrm)
        {
            reprojected.clear();
            reprojected.reserve(lm_coords_in_cam.size());

            for (const auto &lm_coord_in_cam : lm_coords_in_cam)
            {
                Vec2_t reproj;
                float x_right;
                keyfrm->camera_->reproject_to_image(Mat33_t::Identity(), Vec3_t::Zero(), lm_coord_in_cam, reproj, x_right);

                reprojected.push_back(reproj);
            }
        }

    } // namespace solve
} // namespace PLSLAM
