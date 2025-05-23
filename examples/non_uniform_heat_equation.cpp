// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

//! [includes]
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <ddc/ddc.hpp>

#include <Kokkos_Core.hpp>
//! [includes]

//! [vector_generator]
std::vector<double> generate_random_vector(int n, double lower_bound, double higher_bound)
{
    assert(n > 1);
    assert(lower_bound < higher_bound);

    std::random_device rd;
    std::mt19937 gen(rd());
    // p represents the fraction of displacement
    // it should be less than 0.5 to avoid reordering of nodes
    double const p = 0.1;
    std::uniform_real_distribution<double> dis(-p, +p);

    double const dx = (higher_bound - lower_bound) / (n - 1);

    std::vector<double> points(n);

    // Generate a uniform mesh
    for (int i = 0; i < n; ++i) {
        points[i] = lower_bound + i * dx;
    }
    // Add a random perturbation
    for (int i = 1; i < n - 1; ++i) {
        points[i] += dis(gen) * dx;
    }

    assert(std::is_sorted(points.begin(), points.end()));

    return points;
}
//! [vector_generator]

std::vector<double> periodic_extrapolation_left(int gw, std::vector<double> const& points)
{
    assert(gw >= 0);
    assert(points.size() > gw);
    assert(std::is_sorted(points.begin(), points.end()));

    std::vector<double> ghost(gw);
    if (gw > 0) {
        double const period = points.back() - points.front();
        auto const it = std::next(points.crbegin());
        std::transform(it, std::next(it, gw), ghost.rbegin(), [period](double pos) {
            return pos - period;
        });
    }

    return ghost;
}

std::vector<double> periodic_extrapolation_right(int gw, std::vector<double> const& points)
{
    assert(gw >= 0);
    assert(points.size() > gw);
    assert(std::is_sorted(points.begin(), points.end()));

    std::vector<double> ghost(gw);
    if (gw > 0) {
        double const period = points.back() - points.front();
        auto const it = std::next(points.cbegin());
        std::transform(it, std::next(it, gw), ghost.begin(), [period](double pos) {
            return pos + period;
        });
    }

    return ghost;
}

//! [X-dimension]
struct X;
//! [X-dimension]

//! [X-discretization]
struct DDimX : ddc::NonUniformPointSampling<X>
{
};
//! [X-discretization]

//! [Y-space]
struct Y;
struct DDimY : ddc::NonUniformPointSampling<Y>
{
};
//! [Y-space]

//! [time-space]
struct T;
struct DDimT : ddc::UniformPointSampling<T>
{
};
//! [time-space]

/** A function to pretty print the temperature
 * @tparam ChunkType The type of chunk span. This way the template parameters are avoided,
 *                   should be deduced by the compiler.
 * @param time The time at which the output is made.
 * @param temp The temperature at this time-step.
 */
//! [display]
template <class ChunkType>
void display(double time, ChunkType temp)
{
    double const mean_temp
            = ddc::transform_reduce(temp.domain(), 0., ddc::reducer::sum<double>(), temp)
              / temp.domain().size();
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "At t = " << time << ",\n";
    std::cout << "  * mean temperature  = " << mean_temp << "\n";
    ddc::ChunkSpan const temp_slice
            = temp[ddc::get_domain<DDimY>(temp).front() + ddc::get_domain<DDimY>(temp).size() / 2];
    std::cout << "  * temperature[y:" << ddc::get_domain<DDimY>(temp).size() / 2 << "] = {";
    ddc::for_each(ddc::get_domain<DDimX>(temp), [=](ddc::DiscreteElement<DDimX> const ix) {
        std::cout << std::setw(6) << temp_slice(ix);
    });
    std::cout << " }\n" << std::flush;
}
//! [display]

int main(int argc, char** argv)
{
    Kokkos::ScopeGuard const kokkos_scope(argc, argv);
    ddc::ScopeGuard const ddc_scope(argc, argv);

    //! [parameters]
    //! [main-start-x-parameters]
    double const x_start = -1.;
    double const x_end = 1.;
    std::size_t const nb_x_points = 10;
    double const kx = .01;
    //! [main-start-x-parameters]
    //! [main-start-y-parameters]
    double const y_start = -1.;
    double const y_end = 1.;
    std::size_t const nb_y_points = 100;
    double const ky = .002;
    //! [main-start-y-parameters]
    //! [main-start-t-parameters]
    double const start_time = 0.;
    double const end_time = 10.;
    //! [main-start-t-parameters]
    std::ptrdiff_t const t_output_period = 10;
    //! [parameters]

    //! [iterator_main-domain]
    std::vector<double> const x_domain_vect = generate_random_vector(nb_x_points, x_start, x_end);
    //! [iterator_main-domain]

    //! [ghost_points_x]
    std::vector<double> const x_pre_ghost_vect = periodic_extrapolation_left(1, x_domain_vect);
    std::vector<double> const x_post_ghost_vect = periodic_extrapolation_right(1, x_domain_vect);
    //! [ghost_points_x]

    //! [build-domains]
    auto const [x_domain, ghosted_x_domain, x_pre_ghost, x_post_ghost]
            = ddc::init_discrete_space<DDimX>(
                    DDimX::init_ghosted<DDimX>(x_domain_vect, x_pre_ghost_vect, x_post_ghost_vect));
    //! [build-domains]

    ddc::DiscreteDomain<DDimX> const
            x_post_mirror(x_post_ghost.front() - x_domain.extents(), x_post_ghost.extents());
    ddc::DiscreteDomain<DDimX> const
            x_pre_mirror(x_pre_ghost.front() + x_domain.extents(), x_pre_ghost.extents());

    //! [Y-vectors]
    std::vector<double> const y_domain_vect = generate_random_vector(nb_y_points, y_start, y_end);

    //! [ghost_points_y]
    std::vector<double> const y_pre_ghost_vect = periodic_extrapolation_left(1, y_domain_vect);
    std::vector<double> const y_post_ghost_vect = periodic_extrapolation_right(1, y_domain_vect);
    //! [ghost_points_y]
    //! [Y-vectors]

    //! [build-Y-domain]
    auto const [y_domain, ghosted_y_domain, y_pre_ghost, y_post_ghost]
            = ddc::init_discrete_space<DDimY>(
                    DDimY::init_ghosted<DDimY>(y_domain_vect, y_pre_ghost_vect, y_post_ghost_vect));
    //! [build-Y-domain]

    ddc::DiscreteDomain<DDimY> const
            y_post_mirror(y_post_ghost.front() - y_domain.extents(), y_post_ghost.extents());

    ddc::DiscreteDomain<DDimY> const
            y_pre_mirror(y_pre_ghost.front() + y_domain.extents(), y_pre_ghost.extents());

    //! [CFL-condition]
    double const invdx2_max = ddc::transform_reduce(
            x_domain,
            0.,
            ddc::reducer::max<double>(),
            [](ddc::DiscreteElement<DDimX> ix) {
                return 1. / (ddc::distance_at_left(ix) * ddc::distance_at_right(ix));
            });

    double const invdy2_max = ddc::transform_reduce(
            y_domain,
            0.,
            ddc::reducer::max<double>(),
            [](ddc::DiscreteElement<DDimY> iy) {
                return 1. / (ddc::distance_at_left(iy) * ddc::distance_at_right(iy));
            });

    ddc::Coordinate<T> const max_dt(.5 / (kx * invdx2_max + ky * invdy2_max));
    //! [CFL-condition]

    //! [time-domain]
    ddc::DiscreteVector<DDimT> const nb_time_steps(
            std::ceil((end_time - start_time) / max_dt) + .2);

    ddc::DiscreteDomain<DDimT> const time_domain
            = ddc::init_discrete_space<DDimT>(DDimT::init<DDimT>(
                    ddc::Coordinate<T>(start_time),
                    ddc::Coordinate<T>(end_time),
                    nb_time_steps + 1));
    //! [time-domain]

    //! [data allocation]
    ddc::Chunk ghosted_last_temp(
            "ghosted_last_temp",
            ddc::DiscreteDomain<DDimX, DDimY>(ghosted_x_domain, ghosted_y_domain),
            ddc::DeviceAllocator<double>());

    ddc::Chunk ghosted_next_temp(
            "ghosted_next_temp",
            ddc::DiscreteDomain<DDimX, DDimY>(ghosted_x_domain, ghosted_y_domain),
            ddc::DeviceAllocator<double>());
    //! [data allocation]

    //! [initial-chunkspan]
    ddc::ChunkSpan const ghosted_initial_temp = ghosted_last_temp.span_view();
    //! [initial-chunkspan]

    //! [fill-initial-chunkspan]
    ddc::parallel_for_each(
            ddc::DiscreteDomain<DDimX, DDimY>(x_domain, y_domain),
            KOKKOS_LAMBDA(ddc::DiscreteElement<DDimX, DDimY> const ixy) {
                double const x = ddc::coordinate(ddc::DiscreteElement<DDimX>(ixy));
                double const y = ddc::coordinate(ddc::DiscreteElement<DDimY>(ixy));
                ghosted_initial_temp(ixy) = 9.999 * ((x * x + y * y) < 0.25);
            });
    //! [fill-initial-chunkspan]

    //! [host-chunk]
    ddc::Chunk ghosted_temp = ddc::create_mirror(ghosted_last_temp.span_cview());
    //! [host-chunk]

    //! [initial-deepcopy]
    ddc::parallel_deepcopy(ghosted_temp, ghosted_last_temp);
    //! [initial-deepcopy]

    //! [initial-display]
    display(ddc::coordinate(time_domain.front()), ghosted_temp[x_domain][y_domain]);
    //! [initial-display]

    //! [last-output-iter]
    ddc::DiscreteElement<DDimT> last_output_iter = time_domain.front();
    //! [last-output-iter]

    //! [time iteration]
    for (ddc::DiscreteElement<DDimT> const iter :
         time_domain.remove_first(ddc::DiscreteVector<DDimT>(1))) {
        //! [time iteration]

        //! [boundary conditions]
        for (ddc::DiscreteVectorElement ix = 0; ix < x_pre_ghost.extents().value(); ++ix) {
            ddc::parallel_deepcopy(
                    ghosted_last_temp[x_pre_ghost[ix]][y_domain],
                    ghosted_last_temp[x_pre_mirror[ix]][y_domain]);
        }
        for (ddc::DiscreteVectorElement ix = 0; ix < x_post_ghost.extents().value(); ++ix) {
            ddc::parallel_deepcopy(
                    ghosted_last_temp[x_post_ghost[ix]][y_domain],
                    ghosted_last_temp[x_post_mirror[ix]][y_domain]);
        }
        for (ddc::DiscreteVectorElement iy = 0; iy < y_pre_ghost.extents().value(); ++iy) {
            ddc::parallel_deepcopy(
                    ghosted_last_temp[x_domain][y_pre_ghost[iy]],
                    ghosted_last_temp[x_domain][y_pre_mirror[iy]]);
        }
        for (ddc::DiscreteVectorElement iy = 0; iy < y_post_ghost.extents().value(); ++iy) {
            ddc::parallel_deepcopy(
                    ghosted_last_temp[x_domain][y_post_ghost[iy]],
                    ghosted_last_temp[x_domain][y_post_mirror[iy]]);
        }
        //! [boundary conditions]

        //! [manipulated views]
        ddc::ChunkSpan const next_temp(
                ghosted_next_temp[ddc::DiscreteDomain<DDimX, DDimY>(x_domain, y_domain)]);
        ddc::ChunkSpan const last_temp(ghosted_last_temp.span_cview());
        //! [manipulated views]

        //! [numerical scheme]
        ddc::parallel_for_each(
                next_temp.domain(),
                KOKKOS_LAMBDA(ddc::DiscreteElement<DDimX, DDimY> const ixy) {
                    ddc::DiscreteElement<DDimX> const ix(ixy);
                    ddc::DiscreteElement<DDimY> const iy(ixy);
                    double const dx_l = ddc::distance_at_left(ix);
                    double const dx_r = ddc::distance_at_right(ix);
                    double const dx_m = 0.5 * (dx_l + dx_r);
                    double const dy_l = ddc::distance_at_left(iy);
                    double const dy_r = ddc::distance_at_right(iy);
                    double const dy_m = 0.5 * (dy_l + dy_r);
                    next_temp(ix, iy) = last_temp(ix, iy);
                    next_temp(ix, iy)
                            += kx * ddc::step<DDimT>()
                               * (dx_l * last_temp(ix + 1, iy) - 2.0 * dx_m * last_temp(ix, iy)
                                  + dx_r * last_temp(ix - 1, iy))
                               / (dx_l * dx_m * dx_r);
                    next_temp(ix, iy)
                            += ky * ddc::step<DDimT>()
                               * (dy_l * last_temp(ix, iy + 1) - 2.0 * dy_m * last_temp(ix, iy)
                                  + dy_r * last_temp(ix, iy - 1))
                               / (dy_l * dy_m * dy_r);
                });
        //! [numerical scheme]

        //! [output]
        if (iter - last_output_iter >= t_output_period) {
            last_output_iter = iter;
            ddc::parallel_deepcopy(ghosted_temp, ghosted_next_temp);
            display(ddc::coordinate(iter),
                    ghosted_temp[ddc::DiscreteDomain<DDimX, DDimY>(x_domain, y_domain)]);
        }
        //! [output]

        //! [swap]
        std::swap(ghosted_last_temp, ghosted_next_temp);
        //! [swap]
    }

    //! [final output]
    if (last_output_iter < time_domain.back()) {
        ddc::parallel_deepcopy(ghosted_temp, ghosted_last_temp);
        display(ddc::coordinate(time_domain.back()),
                ghosted_temp[ddc::DiscreteDomain<DDimX, DDimY>(x_domain, y_domain)]);
    }
    //! [final output]
}
