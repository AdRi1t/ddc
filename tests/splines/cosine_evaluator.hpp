// Copyright (C) The DDC development team, see COPYRIGHT.md file
//
// SPDX-License-Identifier: MIT

#pragma once

#include <ddc/ddc.hpp>
#include <ddc/kernels/splines.hpp>

#include <Kokkos_MathematicalConstants.hpp>

struct CosineEvaluator
{
    template <class DDim>
    class Evaluator
    {
    public:
        using Dim = DDim;

    private:
        static constexpr double s_2_pi = 2 * Kokkos::numbers::pi;

        static constexpr double s_pi_2 = Kokkos::numbers::pi / 2;

    private:
        double m_c0;

        double m_c1;

    public:
        template <class Domain>
        explicit Evaluator([[maybe_unused]] Domain domain) : m_c0(1.0)
                                                           , m_c1(0.0)
        {
        }

        Evaluator(double c0, double c1) : m_c0(c0), m_c1(c1) {}

        KOKKOS_FUNCTION double operator()(double const x) const noexcept
        {
            return eval(x, 0);
        }

        KOKKOS_FUNCTION void operator()(
                ddc::ChunkSpan<double, ddc::DiscreteDomain<DDim>> chunk) const
        {
            ddc::DiscreteDomain<DDim> const domain = chunk.domain();

            for (ddc::DiscreteElement<DDim> const i : domain) {
                chunk(i) = eval(ddc::coordinate(i), 0);
            }
        }

        KOKKOS_FUNCTION double deriv(double const x, int const derivative) const noexcept
        {
            return eval(x, derivative);
        }

        KOKKOS_FUNCTION void deriv(
                ddc::ChunkSpan<double, ddc::DiscreteDomain<DDim>> chunk,
                int const derivative) const
        {
            ddc::DiscreteDomain<DDim> const domain = chunk.domain();

            for (ddc::DiscreteElement<DDim> const i : domain) {
                chunk(i) = eval(ddc::coordinate(i), derivative);
            }
        }

        KOKKOS_FUNCTION double max_norm(int diff = 0) const
        {
            return ddc::detail::ipow(s_2_pi * m_c0, diff);
        }

    private:
        KOKKOS_FUNCTION double eval(double const x, int const derivative) const noexcept
        {
            return ddc::detail::ipow(s_2_pi * m_c0, derivative)
                   * Kokkos::cos(s_pi_2 * derivative + s_2_pi * (m_c0 * x + m_c1));
        }
    };
};
