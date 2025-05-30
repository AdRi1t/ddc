// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <cstddef>
#include <tuple>
#if defined(BC_HERMITE)
#include <optional>
#endif
#if defined(BSPLINES_TYPE_UNIFORM)
#include <type_traits>
#endif
#include <vector>

#include <ddc/ddc.hpp>
#include <ddc/kernels/splines.hpp>

#include <gtest/gtest.h>

#include <Kokkos_Core.hpp>

#include "evaluator_2d.hpp"
#if defined(BC_PERIODIC)
#include "cosine_evaluator.hpp"
#else
#include "polynomial_evaluator.hpp"
#endif
#include "spline_error_bounds.hpp"

namespace DDC_HIP_5_7_ANONYMOUS_NAMESPACE_WORKAROUND(BATCHED_2D_SPLINE_BUILDER_CPP) {

#if defined(BC_PERIODIC)
struct DimX
{
    static constexpr bool PERIODIC = true;
};

struct DimY
{
    static constexpr bool PERIODIC = true;
};
#else

struct DimX
{
    static constexpr bool PERIODIC = false;
};

struct DimY
{
    static constexpr bool PERIODIC = false;
};
#endif

struct DDimBatch
{
};

struct DDimBatchExtra
{
};

constexpr std::size_t s_degree = DEGREE;

#if defined(BC_PERIODIC)
constexpr ddc::BoundCond s_bcl = ddc::BoundCond::PERIODIC;
constexpr ddc::BoundCond s_bcr = ddc::BoundCond::PERIODIC;
#elif defined(BC_GREVILLE)
constexpr ddc::BoundCond s_bcl = ddc::BoundCond::GREVILLE;
constexpr ddc::BoundCond s_bcr = ddc::BoundCond::GREVILLE;
#elif defined(BC_HERMITE)
constexpr ddc::BoundCond s_bcl = ddc::BoundCond::HERMITE;
constexpr ddc::BoundCond s_bcr = ddc::BoundCond::HERMITE;
#endif

template <typename BSpX>
using GrevillePoints = ddc::GrevilleInterpolationPoints<BSpX, s_bcl, s_bcr>;

#if defined(BSPLINES_TYPE_UNIFORM)
template <typename X>
struct BSplines : ddc::UniformBSplines<X, s_degree>
{
};
#elif defined(BSPLINES_TYPE_NON_UNIFORM)
template <typename X>
struct BSplines : ddc::NonUniformBSplines<X, s_degree>
{
};
#endif

// In the dimensions of interest, the discrete dimension is deduced from the Greville points type.
template <typename X>
struct DDimGPS : GrevillePoints<BSplines<X>>::interpolation_discrete_dimension_type
{
};

#if defined(BC_PERIODIC)
template <typename DDim1, typename DDim2>
using evaluator_type = Evaluator2D::
        Evaluator<CosineEvaluator::Evaluator<DDim1>, CosineEvaluator::Evaluator<DDim2>>;
#else
template <typename DDim1, typename DDim2>
using evaluator_type = Evaluator2D::Evaluator<
        PolynomialEvaluator::Evaluator<DDim1, s_degree>,
        PolynomialEvaluator::Evaluator<DDim2, s_degree>>;
#endif

template <typename... DDimX>
using DElem = ddc::DiscreteElement<DDimX...>;
template <typename... DDimX>
using DVect = ddc::DiscreteVector<DDimX...>;
template <typename... X>
using Coord = ddc::Coordinate<X...>;

// Templated function giving first coordinate of the mesh in given dimension.
template <typename X>
KOKKOS_FUNCTION Coord<X> x0()
{
    return Coord<X>(0.);
}

// Templated function giving last coordinate of the mesh in given dimension.
template <typename X>
KOKKOS_FUNCTION Coord<X> xN()
{
    return Coord<X>(1.);
}

// Templated function giving step of the mesh in given dimension.
template <typename X>
double dx(std::size_t ncells)
{
    return (xN<X>() - x0<X>()) / ncells;
}

// Templated function giving break points of mesh in given dimension for non-uniform case.
template <typename X>
std::vector<Coord<X>> breaks(std::size_t ncells)
{
    std::vector<Coord<X>> out(ncells + 1);
    for (std::size_t i(0); i < ncells + 1; ++i) {
        out[i] = x0<X>() + i * dx<X>(ncells);
    }
    return out;
}

template <class DDim>
void InterestDimInitializer(std::size_t const ncells)
{
    using CDim = typename DDim::continuous_dimension_type;
#if defined(BSPLINES_TYPE_UNIFORM)
    ddc::init_discrete_space<BSplines<CDim>>(x0<CDim>(), xN<CDim>(), ncells);
#elif defined(BSPLINES_TYPE_NON_UNIFORM)
    ddc::init_discrete_space<BSplines<CDim>>(breaks<CDim>(ncells));
#endif
    ddc::init_discrete_space<DDim>(GrevillePoints<BSplines<CDim>>::template get_sampling<DDim>());
}

/// Computes the evaluation error when evaluating a 2D spline on its interpolation points.
template <
        typename ExecSpace,
        typename MemorySpace,
        typename DDimI1,
        typename DDimI2,
        typename Builder,
        typename Evaluator,
        typename... DDims>
std::tuple<double, double, double, double> ComputeEvaluationError(
        ExecSpace const& exec_space,
        ddc::DiscreteDomain<DDims...> const& dom_vals,
        Builder const& spline_builder,
        Evaluator const& spline_evaluator,
        evaluator_type<DDimI1, DDimI2> const& evaluator)
{
    using I1 = typename DDimI1::continuous_dimension_type;
    using I2 = typename DDimI2::continuous_dimension_type;

#if defined(BC_HERMITE)
    ddc::DiscreteDomain<DDimI1> const interpolation_domain1(dom_vals);
    ddc::DiscreteDomain<DDimI2> const interpolation_domain2(dom_vals);
    // Create the derivs domain
    ddc::DiscreteDomain<ddc::Deriv<I1>> const
            derivs_domain1(DElem<ddc::Deriv<I1>>(1), DVect<ddc::Deriv<I1>>(s_degree / 2));
    ddc::DiscreteDomain<ddc::Deriv<I2>> const
            derivs_domain2(DElem<ddc::Deriv<I2>>(1), DVect<ddc::Deriv<I2>>(s_degree / 2));
    ddc::DiscreteDomain<ddc::Deriv<I1>, ddc::Deriv<I2>> const
            derivs_domain(derivs_domain1, derivs_domain2);

    auto const dom_derivs_1d
            = ddc::replace_dim_of<DDimI1, ddc::Deriv<I1>>(dom_vals, derivs_domain1);
    auto const dom_derivs2 = ddc::replace_dim_of<DDimI2, ddc::Deriv<I2>>(dom_vals, derivs_domain2);
    auto const dom_derivs
            = ddc::replace_dim_of<DDimI2, ddc::Deriv<I2>>(dom_derivs_1d, derivs_domain2);
#endif

    // Compute useful domains (dom_interpolation, dom_batch, dom_bsplines and dom_spline)
    ddc::DiscreteDomain<DDimI1, DDimI2> const dom_interpolation
            = spline_builder.interpolation_domain();
    auto const dom_spline = spline_builder.batched_spline_domain(dom_vals);

    // Allocate and fill a chunk containing values to be passed as input to spline_builder. Those are values of cosine along interest dimension duplicated along batch dimensions
    ddc::Chunk vals_1d_host_alloc(dom_interpolation, ddc::HostAllocator<double>());
    ddc::ChunkSpan const vals_1d_host = vals_1d_host_alloc.span_view();
    evaluator(vals_1d_host);
    auto vals_1d_alloc = ddc::create_mirror_view_and_copy(exec_space, vals_1d_host);
    ddc::ChunkSpan const vals_1d = vals_1d_alloc.span_view();

    ddc::Chunk vals_alloc(dom_vals, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const vals = vals_alloc.span_view();
    ddc::parallel_for_each(
            exec_space,
            vals.domain(),
            KOKKOS_LAMBDA(DElem<DDims...> const e) {
                vals(e) = vals_1d(DElem<DDimI1, DDimI2>(e));
            });

#if defined(BC_HERMITE)
    // Allocate and fill a chunk containing derivs to be passed as input to spline_builder.
    int const shift = s_degree % 2; // shift = 0 for even order, 1 for odd order
    ddc::Chunk derivs_1d_lhs_alloc(dom_derivs_1d, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const derivs_1d_lhs = derivs_1d_lhs_alloc.span_view();
    if (s_bcl == ddc::BoundCond::HERMITE) {
        ddc::Chunk derivs_1d_lhs1_host_alloc(
                ddc::DiscreteDomain<ddc::Deriv<I1>, DDimI2>(derivs_domain1, interpolation_domain2),
                ddc::HostAllocator<double>());
        ddc::ChunkSpan const derivs_1d_lhs1_host = derivs_1d_lhs1_host_alloc.span_view();
        ddc::for_each(
                derivs_1d_lhs1_host.domain(),
                KOKKOS_LAMBDA(ddc::DiscreteElement<ddc::Deriv<I1>, DDimI2> const e) {
                    auto deriv_idx = ddc::DiscreteElement<ddc::Deriv<I1>>(e).uid();
                    auto x2 = ddc::coordinate(ddc::DiscreteElement<DDimI2>(e));
                    derivs_1d_lhs1_host(e)
                            = evaluator.deriv(x0<I1>(), x2, deriv_idx + shift - 1, 0);
                });
        auto derivs_1d_lhs1_alloc
                = ddc::create_mirror_view_and_copy(exec_space, derivs_1d_lhs1_host);
        ddc::ChunkSpan const derivs_1d_lhs1 = derivs_1d_lhs1_alloc.span_view();

        ddc::parallel_for_each(
                exec_space,
                derivs_1d_lhs.domain(),
                KOKKOS_LAMBDA(
                        typename decltype(derivs_1d_lhs.domain())::discrete_element_type const e) {
                    derivs_1d_lhs(e) = derivs_1d_lhs1(DElem<ddc::Deriv<I1>, DDimI2>(e));
                });
    }

    ddc::Chunk derivs_1d_rhs_alloc(dom_derivs_1d, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const derivs_1d_rhs = derivs_1d_rhs_alloc.span_view();
    if (s_bcl == ddc::BoundCond::HERMITE) {
        ddc::Chunk derivs_1d_rhs1_host_alloc(
                ddc::DiscreteDomain<ddc::Deriv<I1>, DDimI2>(derivs_domain1, interpolation_domain2),
                ddc::HostAllocator<double>());
        ddc::ChunkSpan const derivs_1d_rhs1_host = derivs_1d_rhs1_host_alloc.span_view();
        ddc::for_each(
                derivs_1d_rhs1_host.domain(),
                KOKKOS_LAMBDA(ddc::DiscreteElement<ddc::Deriv<I1>, DDimI2> const e) {
                    auto deriv_idx = ddc::DiscreteElement<ddc::Deriv<I1>>(e).uid();
                    auto x2 = ddc::coordinate(ddc::DiscreteElement<DDimI2>(e));
                    derivs_1d_rhs1_host(e)
                            = evaluator.deriv(xN<I1>(), x2, deriv_idx + shift - 1, 0);
                });
        auto derivs_1d_rhs1_alloc
                = ddc::create_mirror_view_and_copy(exec_space, derivs_1d_rhs1_host);
        ddc::ChunkSpan const derivs_1d_rhs1 = derivs_1d_rhs1_alloc.span_view();

        ddc::parallel_for_each(
                exec_space,
                derivs_1d_rhs.domain(),
                KOKKOS_LAMBDA(
                        typename decltype(derivs_1d_rhs.domain())::discrete_element_type const e) {
                    derivs_1d_rhs(e) = derivs_1d_rhs1(DElem<ddc::Deriv<I1>, DDimI2>(e));
                });
    }

    ddc::Chunk derivs2_lhs_alloc(dom_derivs2, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const derivs2_lhs = derivs2_lhs_alloc.span_view();
    if (s_bcl == ddc::BoundCond::HERMITE) {
        ddc::Chunk derivs2_lhs1_host_alloc(
                ddc::DiscreteDomain<DDimI1, ddc::Deriv<I2>>(interpolation_domain1, derivs_domain2),
                ddc::HostAllocator<double>());
        ddc::ChunkSpan const derivs2_lhs1_host = derivs2_lhs1_host_alloc.span_view();
        ddc::for_each(
                derivs2_lhs1_host.domain(),
                KOKKOS_LAMBDA(ddc::DiscreteElement<DDimI1, ddc::Deriv<I2>> const e) {
                    auto x1 = ddc::coordinate(ddc::DiscreteElement<DDimI1>(e));
                    auto deriv_idx = ddc::DiscreteElement<ddc::Deriv<I2>>(e).uid();
                    derivs2_lhs1_host(e) = evaluator.deriv(x1, x0<I2>(), 0, deriv_idx + shift - 1);
                });

        auto derivs2_lhs1_alloc = ddc::create_mirror_view_and_copy(exec_space, derivs2_lhs1_host);
        ddc::ChunkSpan const derivs2_lhs1 = derivs2_lhs1_alloc.span_view();

        ddc::parallel_for_each(
                exec_space,
                derivs2_lhs.domain(),
                KOKKOS_LAMBDA(
                        typename decltype(derivs2_lhs.domain())::discrete_element_type const e) {
                    derivs2_lhs(e) = derivs2_lhs1(DElem<DDimI1, ddc::Deriv<I2>>(e));
                });
    }

    ddc::Chunk derivs2_rhs_alloc(dom_derivs2, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const derivs2_rhs = derivs2_rhs_alloc.span_view();
    if (s_bcl == ddc::BoundCond::HERMITE) {
        ddc::Chunk derivs2_rhs1_host_alloc(
                ddc::DiscreteDomain<DDimI1, ddc::Deriv<I2>>(interpolation_domain1, derivs_domain2),
                ddc::HostAllocator<double>());
        ddc::ChunkSpan const derivs2_rhs1_host = derivs2_rhs1_host_alloc.span_view();
        ddc::for_each(
                derivs2_rhs1_host.domain(),
                KOKKOS_LAMBDA(ddc::DiscreteElement<DDimI1, ddc::Deriv<I2>> const e) {
                    auto x1 = ddc::coordinate(ddc::DiscreteElement<DDimI1>(e));
                    auto deriv_idx = ddc::DiscreteElement<ddc::Deriv<I2>>(e).uid();
                    derivs2_rhs1_host(e) = evaluator.deriv(x1, xN<I2>(), 0, deriv_idx + shift - 1);
                });

        auto derivs2_rhs1_alloc = ddc::create_mirror_view_and_copy(exec_space, derivs2_rhs1_host);
        ddc::ChunkSpan const derivs2_rhs1 = derivs2_rhs1_alloc.span_view();

        ddc::parallel_for_each(
                exec_space,
                derivs2_rhs.domain(),
                KOKKOS_LAMBDA(
                        typename decltype(derivs2_rhs.domain())::discrete_element_type const e) {
                    derivs2_rhs(e) = derivs2_rhs1(DElem<DDimI1, ddc::Deriv<I2>>(e));
                });
    }

    ddc::Chunk derivs_mixed_lhs_lhs_alloc(dom_derivs, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const derivs_mixed_lhs_lhs = derivs_mixed_lhs_lhs_alloc.span_view();
    ddc::Chunk derivs_mixed_rhs_lhs_alloc(dom_derivs, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const derivs_mixed_rhs_lhs = derivs_mixed_rhs_lhs_alloc.span_view();
    ddc::Chunk derivs_mixed_lhs_rhs_alloc(dom_derivs, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const derivs_mixed_lhs_rhs = derivs_mixed_lhs_rhs_alloc.span_view();
    ddc::Chunk derivs_mixed_rhs_rhs_alloc(dom_derivs, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const derivs_mixed_rhs_rhs = derivs_mixed_rhs_rhs_alloc.span_view();

    if (s_bcl == ddc::BoundCond::HERMITE) {
        ddc::Chunk derivs_mixed_lhs_lhs1_host_alloc(derivs_domain, ddc::HostAllocator<double>());
        ddc::ChunkSpan const derivs_mixed_lhs_lhs1_host
                = derivs_mixed_lhs_lhs1_host_alloc.span_view();
        ddc::Chunk derivs_mixed_rhs_lhs1_host_alloc(derivs_domain, ddc::HostAllocator<double>());
        ddc::ChunkSpan const derivs_mixed_rhs_lhs1_host
                = derivs_mixed_rhs_lhs1_host_alloc.span_view();
        ddc::Chunk derivs_mixed_lhs_rhs1_host_alloc(derivs_domain, ddc::HostAllocator<double>());
        ddc::ChunkSpan const derivs_mixed_lhs_rhs1_host
                = derivs_mixed_lhs_rhs1_host_alloc.span_view();
        ddc::Chunk derivs_mixed_rhs_rhs1_host_alloc(derivs_domain, ddc::HostAllocator<double>());
        ddc::ChunkSpan const derivs_mixed_rhs_rhs1_host
                = derivs_mixed_rhs_rhs1_host_alloc.span_view();

        for (std::size_t ii = 1;
             ii < static_cast<std::size_t>(derivs_domain.template extent<ddc::Deriv<I1>>()) + 1;
             ++ii) {
            for (std::size_t jj = 1;
                 jj < static_cast<std::size_t>(derivs_domain.template extent<ddc::Deriv<I2>>()) + 1;
                 ++jj) {
                derivs_mixed_lhs_lhs1_host(
                        typename decltype(derivs_domain)::discrete_element_type(ii, jj))
                        = evaluator.deriv(x0<I1>(), x0<I2>(), ii + shift - 1, jj + shift - 1);
                derivs_mixed_rhs_lhs1_host(
                        typename decltype(derivs_domain)::discrete_element_type(ii, jj))
                        = evaluator.deriv(xN<I1>(), x0<I2>(), ii + shift - 1, jj + shift - 1);
                derivs_mixed_lhs_rhs1_host(
                        typename decltype(derivs_domain)::discrete_element_type(ii, jj))
                        = evaluator.deriv(x0<I1>(), xN<I2>(), ii + shift - 1, jj + shift - 1);
                derivs_mixed_rhs_rhs1_host(
                        typename decltype(derivs_domain)::discrete_element_type(ii, jj))
                        = evaluator.deriv(xN<I1>(), xN<I2>(), ii + shift - 1, jj + shift - 1);
            }
        }
        auto derivs_mixed_lhs_lhs1_alloc
                = ddc::create_mirror_view_and_copy(exec_space, derivs_mixed_lhs_lhs1_host);
        ddc::ChunkSpan const derivs_mixed_lhs_lhs1 = derivs_mixed_lhs_lhs1_alloc.span_view();
        auto derivs_mixed_rhs_lhs1_alloc
                = ddc::create_mirror_view_and_copy(exec_space, derivs_mixed_rhs_lhs1_host);
        ddc::ChunkSpan const derivs_mixed_rhs_lhs1 = derivs_mixed_rhs_lhs1_alloc.span_view();
        auto derivs_mixed_lhs_rhs1_alloc
                = ddc::create_mirror_view_and_copy(exec_space, derivs_mixed_lhs_rhs1_host);
        ddc::ChunkSpan const derivs_mixed_lhs_rhs1 = derivs_mixed_lhs_rhs1_alloc.span_view();
        auto derivs_mixed_rhs_rhs1_alloc
                = ddc::create_mirror_view_and_copy(exec_space, derivs_mixed_rhs_rhs1_host);
        ddc::ChunkSpan const derivs_mixed_rhs_rhs1 = derivs_mixed_rhs_rhs1_alloc.span_view();

        ddc::parallel_for_each(
                exec_space,
                dom_derivs,
                KOKKOS_LAMBDA(typename decltype(dom_derivs)::discrete_element_type const e) {
                    derivs_mixed_lhs_lhs(e)
                            = derivs_mixed_lhs_lhs1(DElem<ddc::Deriv<I1>, ddc::Deriv<I2>>(e));
                    derivs_mixed_rhs_lhs(e)
                            = derivs_mixed_rhs_lhs1(DElem<ddc::Deriv<I1>, ddc::Deriv<I2>>(e));
                    derivs_mixed_lhs_rhs(e)
                            = derivs_mixed_lhs_rhs1(DElem<ddc::Deriv<I1>, ddc::Deriv<I2>>(e));
                    derivs_mixed_rhs_rhs(e)
                            = derivs_mixed_rhs_rhs1(DElem<ddc::Deriv<I1>, ddc::Deriv<I2>>(e));
                });
    }
#endif

    // Instantiate chunk of spline coefs to receive output of spline_builder
    ddc::Chunk coef_alloc(dom_spline, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const coef = coef_alloc.span_view();

    // Finally compute the spline by filling `coef`
#if defined(BC_HERMITE)
    spline_builder(
            coef,
            vals.span_cview(),
            std::optional(derivs_1d_lhs.span_cview()),
            std::optional(derivs_1d_rhs.span_cview()),
            std::optional(derivs2_lhs.span_cview()),
            std::optional(derivs2_rhs.span_cview()),
            std::optional(derivs_mixed_lhs_lhs.span_cview()),
            std::optional(derivs_mixed_rhs_lhs.span_cview()),
            std::optional(derivs_mixed_lhs_rhs.span_cview()),
            std::optional(derivs_mixed_rhs_rhs.span_cview()));
#else
    spline_builder(coef, vals.span_cview());
#endif

    // Instantiate chunk of coordinates of dom_interpolation
    ddc::Chunk coords_eval_alloc(dom_vals, ddc::KokkosAllocator<Coord<I1, I2>, MemorySpace>());
    ddc::ChunkSpan const coords_eval = coords_eval_alloc.span_view();
    ddc::parallel_for_each(
            exec_space,
            coords_eval.domain(),
            KOKKOS_LAMBDA(DElem<DDims...> const e) {
                coords_eval(e) = ddc::coordinate(DElem<DDimI1, DDimI2>(e));
            });


    // Instantiate chunks to receive outputs of spline_evaluator
    ddc::Chunk spline_eval_alloc(dom_vals, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const spline_eval = spline_eval_alloc.span_view();
    ddc::Chunk spline_eval_deriv1_alloc(dom_vals, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const spline_eval_deriv1 = spline_eval_deriv1_alloc.span_view();
    ddc::Chunk spline_eval_deriv2_alloc(dom_vals, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const spline_eval_deriv2 = spline_eval_deriv2_alloc.span_view();
    ddc::Chunk spline_eval_deriv12_alloc(dom_vals, ddc::KokkosAllocator<double, MemorySpace>());
    ddc::ChunkSpan const spline_eval_deriv12 = spline_eval_deriv12_alloc.span_view();

    // Call spline_evaluator on the same mesh we started with
    spline_evaluator(spline_eval, coords_eval.span_cview(), coef.span_cview());
    spline_evaluator
            .template deriv<I1>(spline_eval_deriv1, coords_eval.span_cview(), coef.span_cview());
    spline_evaluator
            .template deriv<I2>(spline_eval_deriv2, coords_eval.span_cview(), coef.span_cview());
    spline_evaluator.template deriv2<
            I1,
            I2>(spline_eval_deriv12, coords_eval.span_cview(), coef.span_cview());

    // Checking errors (we recover the initial values)
    double const max_norm_error = ddc::parallel_transform_reduce(
            exec_space,
            spline_eval.domain(),
            0.,
            ddc::reducer::max<double>(),
            KOKKOS_LAMBDA(DElem<DDims...> const e) {
                return Kokkos::abs(spline_eval(e) - vals(e));
            });
    double const max_norm_error_diff1 = ddc::parallel_transform_reduce(
            exec_space,
            spline_eval_deriv1.domain(),
            0.,
            ddc::reducer::max<double>(),
            KOKKOS_LAMBDA(DElem<DDims...> const e) {
                Coord<I1> const x = ddc::coordinate(DElem<DDimI1>(e));
                Coord<I2> const y = ddc::coordinate(DElem<DDimI2>(e));
                return Kokkos::abs(spline_eval_deriv1(e) - evaluator.deriv(x, y, 1, 0));
            });
    double const max_norm_error_diff2 = ddc::parallel_transform_reduce(
            exec_space,
            spline_eval_deriv2.domain(),
            0.,
            ddc::reducer::max<double>(),
            KOKKOS_LAMBDA(DElem<DDims...> const e) {
                Coord<I1> const x = ddc::coordinate(DElem<DDimI1>(e));
                Coord<I2> const y = ddc::coordinate(DElem<DDimI2>(e));
                return Kokkos::abs(spline_eval_deriv2(e) - evaluator.deriv(x, y, 0, 1));
            });
    double const max_norm_error_diff12 = ddc::parallel_transform_reduce(
            exec_space,
            spline_eval_deriv1.domain(),
            0.,
            ddc::reducer::max<double>(),
            KOKKOS_LAMBDA(DElem<DDims...> const e) {
                Coord<I1> const x = ddc::coordinate(DElem<DDimI1>(e));
                Coord<I2> const y = ddc::coordinate(DElem<DDimI2>(e));
                return Kokkos::abs(spline_eval_deriv12(e) - evaluator.deriv(x, y, 1, 1));
            });

    return std::make_tuple(
            max_norm_error,
            max_norm_error_diff1,
            max_norm_error_diff2,
            max_norm_error_diff12);
}

// Checks that when evaluating the spline at interpolation points one
// recovers values that were used to build the spline
template <
        typename ExecSpace,
        typename MemorySpace,
        typename DDimI1,
        typename DDimI2,
        typename... DDims>
void MultipleBatchDomain2dSplineTest()
{
    using I1 = typename DDimI1::continuous_dimension_type;
    using I2 = typename DDimI2::continuous_dimension_type;

    // Instantiate execution spaces and initialize spaces
    ExecSpace const exec_space;
    std::size_t const ncells = 10;
    InterestDimInitializer<DDimI1>(ncells);
    InterestDimInitializer<DDimI2>(ncells);

    // Create the values domain (mesh)
    ddc::DiscreteDomain<DDimI1> const interpolation_domain1
            = GrevillePoints<BSplines<I1>>::template get_domain<DDimI1>();
    ddc::DiscreteDomain<DDimI2> const interpolation_domain2
            = GrevillePoints<BSplines<I2>>::template get_domain<DDimI2>();
    ddc::DiscreteDomain<DDimI1, DDimI2> const
            interpolation_domain(interpolation_domain1, interpolation_domain2);
    // The following line creates a discrete domain over all dimensions (DDims...) except DDimI1 and DDimI2.
    auto const dom_vals_tmp = ddc::remove_dims_of_t<ddc::DiscreteDomain<DDims...>, DDimI1, DDimI2>(
            ddc::DiscreteDomain<DDims>(DElem<DDims>(0), DVect<DDims>(ncells))...);
    ddc::DiscreteDomain<DDims...> const
            dom_vals(dom_vals_tmp, interpolation_domain1, interpolation_domain2);

    ddc::DiscreteDomain<DDimBatchExtra> const
            extra_batch_domain(DElem<DDimBatchExtra>(0), DVect<DDimBatchExtra>(ncells));
    ddc::DiscreteDomain<DDims..., DDimBatchExtra> const dom_vals_extra(
            dom_vals_tmp,
            interpolation_domain1,
            interpolation_domain2,
            extra_batch_domain);

    // Create a SplineBuilder over BSplines<I1> and BSplines<I2> and using some boundary conditions
    ddc::SplineBuilder2D<
            ExecSpace,
            MemorySpace,
            BSplines<I1>,
            BSplines<I2>,
            DDimI1,
            DDimI2,
            s_bcl,
            s_bcr,
            s_bcl,
            s_bcr,
            ddc::SplineSolver::GINKGO> const spline_builder(interpolation_domain);

    evaluator_type<DDimI1, DDimI2> const evaluator(spline_builder.interpolation_domain());

    // Instantiate a SplineEvaluator over interest dimensions
#if defined(BC_PERIODIC)
    using extrapolation_rule_1_type = ddc::PeriodicExtrapolationRule<I1>;
    using extrapolation_rule_2_type = ddc::PeriodicExtrapolationRule<I2>;
#else
    using extrapolation_rule_1_type = ddc::NullExtrapolationRule;
    using extrapolation_rule_2_type = ddc::NullExtrapolationRule;
#endif
    extrapolation_rule_1_type const extrapolation_rule_1;
    extrapolation_rule_2_type const extrapolation_rule_2;

    ddc::SplineEvaluator2D<
            ExecSpace,
            MemorySpace,
            BSplines<I1>,
            BSplines<I2>,
            DDimI1,
            DDimI2,
            extrapolation_rule_1_type,
            extrapolation_rule_1_type,
            extrapolation_rule_2_type,
            extrapolation_rule_2_type> const
            spline_evaluator(
                    extrapolation_rule_1,
                    extrapolation_rule_1,
                    extrapolation_rule_2,
                    extrapolation_rule_2);

    // Check the evaluation error for the first domain
    auto const [max_norm_error, max_norm_error_diff1, max_norm_error_diff2, max_norm_error_diff12]
            = ComputeEvaluationError<
                    ExecSpace,
                    MemorySpace>(exec_space, dom_vals, spline_builder, spline_evaluator, evaluator);

    double const max_norm = evaluator.max_norm();
    double const max_norm_diff1 = evaluator.max_norm(1, 0);
    double const max_norm_diff2 = evaluator.max_norm(0, 1);
    double const max_norm_diff12 = evaluator.max_norm(1, 1);

    SplineErrorBounds<evaluator_type<DDimI1, DDimI2>> const error_bounds(evaluator);
    EXPECT_LE(
            max_norm_error,
            std::
                    max(error_bounds
                                .error_bound(dx<I1>(ncells), dx<I2>(ncells), s_degree, s_degree),
                        1.0e-14 * max_norm));
    EXPECT_LE(
            max_norm_error_diff1,
            std::
                    max(error_bounds.error_bound_on_deriv_1(
                                dx<I1>(ncells),
                                dx<I2>(ncells),
                                s_degree,
                                s_degree),
                        1e-12 * max_norm_diff1));
    EXPECT_LE(
            max_norm_error_diff2,
            std::
                    max(error_bounds.error_bound_on_deriv_2(
                                dx<I1>(ncells),
                                dx<I2>(ncells),
                                s_degree,
                                s_degree),
                        1e-12 * max_norm_diff2));
    EXPECT_LE(
            max_norm_error_diff12,
            std::
                    max(error_bounds.error_bound_on_deriv_12(
                                dx<I1>(ncells),
                                dx<I2>(ncells),
                                s_degree,
                                s_degree),
                        1e-11 * max_norm_diff12));

    // Check the evaluation error for the domain with an additional batch dimension
    auto const
            [max_norm_error_extra,
             max_norm_error_diff1_extra,
             max_norm_error_diff2_extra,
             max_norm_error_diff12_extra]
            = ComputeEvaluationError<ExecSpace, MemorySpace>(
                    exec_space,
                    dom_vals_extra,
                    spline_builder,
                    spline_evaluator,
                    evaluator);

    double const max_norm_extra = evaluator.max_norm();
    double const max_norm_diff1_extra = evaluator.max_norm(1, 0);
    double const max_norm_diff2_extra = evaluator.max_norm(0, 1);
    double const max_norm_diff12_extra = evaluator.max_norm(1, 1);

    SplineErrorBounds<evaluator_type<DDimI1, DDimI2>> const error_bounds_extra(evaluator);
    EXPECT_LE(
            max_norm_error_extra,
            std::
                    max(error_bounds_extra
                                .error_bound(dx<I1>(ncells), dx<I2>(ncells), s_degree, s_degree),
                        1.0e-14 * max_norm_extra));
    EXPECT_LE(
            max_norm_error_diff1_extra,
            std::
                    max(error_bounds_extra.error_bound_on_deriv_1(
                                dx<I1>(ncells),
                                dx<I2>(ncells),
                                s_degree,
                                s_degree),
                        1e-12 * max_norm_diff1_extra));
    EXPECT_LE(
            max_norm_error_diff2_extra,
            std::
                    max(error_bounds_extra.error_bound_on_deriv_2(
                                dx<I1>(ncells),
                                dx<I2>(ncells),
                                s_degree,
                                s_degree),
                        1e-12 * max_norm_diff2_extra));
    EXPECT_LE(
            max_norm_error_diff12_extra,
            std::
                    max(error_bounds_extra.error_bound_on_deriv_12(
                                dx<I1>(ncells),
                                dx<I2>(ncells),
                                s_degree,
                                s_degree),
                        1e-11 * max_norm_diff12_extra));
}

} // namespace DDC_HIP_5_7_ANONYMOUS_NAMESPACE_WORKAROUND(BATCHED_2D_SPLINE_BUILDER_CPP)

#if defined(BC_PERIODIC) && defined(BSPLINES_TYPE_UNIFORM)
#define SUFFIX(name) name##Periodic##Uniform
#elif defined(BC_PERIODIC) && defined(BSPLINES_TYPE_NON_UNIFORM)
#define SUFFIX(name) name##Periodic##NonUniform
#elif defined(BC_GREVILLE) && defined(BSPLINES_TYPE_UNIFORM)
#define SUFFIX(name) name##Greville##Uniform
#elif defined(BC_GREVILLE) && defined(BSPLINES_TYPE_NON_UNIFORM)
#define SUFFIX(name) name##Greville##NonUniform
#elif defined(BC_HERMITE) && defined(BSPLINES_TYPE_UNIFORM)
#define SUFFIX(name) name##Hermite##Uniform
#elif defined(BC_HERMITE) && defined(BSPLINES_TYPE_NON_UNIFORM)
#define SUFFIX(name) name##Hermite##NonUniform
#endif

TEST(SUFFIX(MultipleBatchDomain2dSpline), 2DXY)
{
    MultipleBatchDomain2dSplineTest<
            Kokkos::DefaultExecutionSpace,
            Kokkos::DefaultExecutionSpace::memory_space,
            DDimGPS<DimX>,
            DDimGPS<DimY>,
            DDimGPS<DimX>,
            DDimGPS<DimY>>();
}

TEST(SUFFIX(MultipleBatchDomain2dSpline), 3DXYB)
{
    MultipleBatchDomain2dSplineTest<
            Kokkos::DefaultExecutionSpace,
            Kokkos::DefaultExecutionSpace::memory_space,
            DDimGPS<DimX>,
            DDimGPS<DimY>,
            DDimGPS<DimX>,
            DDimGPS<DimY>,
            DDimBatch>();
}

TEST(SUFFIX(MultipleBatchDomain2dSpline), 3DXBY)
{
    MultipleBatchDomain2dSplineTest<
            Kokkos::DefaultExecutionSpace,
            Kokkos::DefaultExecutionSpace::memory_space,
            DDimGPS<DimX>,
            DDimGPS<DimY>,
            DDimGPS<DimX>,
            DDimBatch,
            DDimGPS<DimY>>();
}

TEST(SUFFIX(MultipleBatchDomain2dSpline), 3DBXY)
{
    MultipleBatchDomain2dSplineTest<
            Kokkos::DefaultExecutionSpace,
            Kokkos::DefaultExecutionSpace::memory_space,
            DDimGPS<DimX>,
            DDimGPS<DimY>,
            DDimBatch,
            DDimGPS<DimX>,
            DDimGPS<DimY>>();
}
