// SPDX-License-Identifier: MIT
#include <iosfwd>
#include <memory>
#include <type_traits>

#include <experimental/mdspan>

#include <ddc/Block>
#include <ddc/BlockSpan>
#include <ddc/MCoord>
#include <ddc/ProductMDomain>
#include <ddc/RCoord>
#include <ddc/TaggedVector>
#include <ddc/UniformMesh>

#include <gtest/gtest.h>

using namespace std;
using namespace std::experimental;

class DimX;
class DimVx;

using MeshX = UniformMesh<DimX>;
using MCoordX = MCoord<MeshX>;
using MLengthX = MLength<MeshX>;
using RCoordX = MeshX::rcoord_type;
using MeshVx = UniformMesh<DimVx>;
using RCoordVx = MeshVx::rcoord_type;
using MDomainX = ProductMDomain<MeshX>;
using DBlockX = Block<double, MDomainX>;
using RCoordXVx = RCoord<DimX, DimVx>;
using MCoordXVx = MCoord<MeshX, MeshVx>;
using MLengthXVx = MLength<MeshX, MeshVx>;
using MDomainSpXVx = ProductMDomain<MeshX, MeshVx>;
using DBlockSpXVx = Block<double, MDomainSpXVx>;
using RCoordVxX = RCoord<DimVx, DimX>;
using MCoordVxX = MCoord<MeshVx, MeshX>;
using MDomainVxX = ProductMDomain<MeshVx, MeshX>;
using DBlockVxX = Block<double, MDomainVxX>;

class DBlockXTest : public ::testing::Test
{
protected:
    MeshX mesh = MeshX(RCoordX(0.0), RCoordX(1.0));
    MDomainX dom = MDomainX(mesh, MCoordX(10), MLengthX(91));
};

TEST_F(DBlockXTest, constructor)
{
    DBlockX block(dom);
}

TEST_F(DBlockXTest, domain)
{
    DBlockX block(dom);
    ASSERT_EQ(dom, block.domain());
}

TEST_F(DBlockXTest, domainX)
{
    DBlockX block(dom);
    ASSERT_EQ(dom, block.domain<MeshX>());
}

TEST_F(DBlockXTest, get_domainX)
{
    DBlockX block(dom);
    ASSERT_EQ(dom, get_domain<MeshX>(block));
}

TEST_F(DBlockXTest, access)
{
    DBlockX block(dom);
    for (auto&& ii : block.domain()) {
        ASSERT_EQ(block(ii), block(ii));
    }
}

TEST_F(DBlockXTest, deepcopy)
{
    DBlockX block(dom);
    for (auto&& ii : block.domain()) {
        block(ii) = 1.001 * ii;
    }
    DBlockX block2(block.domain());
    deepcopy(block2, block);
    for (auto&& ii : block.domain()) {
        // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
        ASSERT_EQ(block2(ii), block(ii));
    }
}

class DBlockXVxTest : public ::testing::Test
{
protected:
    MeshX mesh_x = MeshX(RCoordX(0.0), RCoordX(1.0));
    MeshVx mesh_vx = MeshVx(RCoordVx(0.0), RCoordVx(1.0));
    MDomainSpXVx dom = MDomainSpXVx(mesh_x, mesh_vx, MCoordXVx(0, 0), MLengthXVx(101, 101));
};

TEST_F(DBlockXVxTest, deepcopy)
{
    DBlockSpXVx block(dom);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            block(ii, jj) = 1. * ii + .001 * jj;
        }
    }
    DBlockSpXVx block2(block.domain());
    deepcopy(block2, block);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
            ASSERT_EQ(block2(ii, jj), block(ii, jj));
        }
    }
}

TEST_F(DBlockXVxTest, reordering)
{
    DBlockSpXVx block(dom);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            block(ii, jj) = 1. * ii + .001 * jj;
        }
    }

    MDomainVxX dom_reordered = select<MeshVx, MeshX>(dom);
    DBlockVxX block_reordered(dom_reordered);
    deepcopy(block_reordered, block);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
            ASSERT_EQ(block_reordered(jj, ii), block(ii, jj));
        }
    }
}

TEST_F(DBlockXVxTest, slice)
{
    DBlockSpXVx block(dom);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            block(ii, jj) = 1. * ii + .001 * jj;
        }
    }
    ASSERT_TRUE((std::is_same_v<
                 std::decay_t<decltype(block)>::layout_type,
                 std::experimental::layout_right>));
    {
        const DBlockSpXVx& constref_block = block;
        constexpr auto SLICE_VAL = 1;
        auto&& block_x = constref_block[MCoord<MeshVx>(SLICE_VAL)];
        ASSERT_TRUE((std::is_same_v<
                     std::decay_t<decltype(block_x)>::layout_type,
                     std::experimental::layout_stride>));
        ASSERT_EQ(block_x.extent<MeshX>(), block.extent<MeshX>());
        for (auto&& ii : constref_block.domain<MeshX>()) {
            // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
            ASSERT_EQ(block_x(ii), constref_block(ii, MCoord<MeshVx>(SLICE_VAL)));
        }

        auto&& block_v = constref_block[MCoord<MeshX>(SLICE_VAL)];
        ASSERT_TRUE((std::is_same_v<
                     std::decay_t<decltype(block_v)>::layout_type,
                     std::experimental::layout_right>));
        ASSERT_EQ(block_v.extent<MeshVx>(), block.extent<MeshVx>());
        for (auto&& ii : constref_block.domain<MeshVx>()) {
            // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
            ASSERT_EQ(block_v(ii), constref_block(MCoord<MeshX>(SLICE_VAL), ii));
        }

        auto&& subblock = constref_block[ProductMDomain<MeshX>(mesh_x, MCoordX(10), MLengthX(5))];
        // ASSERT_TRUE((std::is_same_v<
        //              std::decay_t<decltype(subblock)>::layout_type,
        //              std::experimental::layout_right>));
        ASSERT_EQ(subblock.extent<MeshX>(), 5);
        ASSERT_EQ(subblock.extent<MeshVx>(), select<MeshVx>(block.domain()).size());
        for (auto&& ii : subblock.domain<MeshX>()) {
            for (auto&& jj : subblock.domain<MeshVx>()) {
                // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
                ASSERT_EQ(subblock(ii, jj), constref_block(ii, jj));
            }
        }
    }
}

TEST_F(DBlockXVxTest, view)
{
    DBlockSpXVx block(dom);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            block(ii, jj) = 1. * ii + .001 * jj;
        }
    }
    auto cview = block.cview();
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
            ASSERT_EQ(cview(ii, jj), block(ii, jj));
        }
    }
}

TEST_F(DBlockXVxTest, automatic_reordering)
{
    DBlockSpXVx block(dom);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            block(ii, jj) = 1. * ii + .001 * jj;
        }
    }
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            ASSERT_EQ(block(jj, ii), block(ii, jj));
        }
    }
}

class NonZeroDBlockXVxTest : public ::testing::Test
{
protected:
    MeshX mesh_x = MeshX(RCoordX(0.0), RCoordX(1.0));
    MeshVx mesh_vx = MeshVx(RCoordVx(0.0), RCoordVx(1.0));
    MDomainSpXVx dom = MDomainSpXVx(mesh_x, mesh_vx, MCoordXVx(100, 100), MLengthXVx(101, 101));
};

TEST_F(NonZeroDBlockXVxTest, view)
{
    DBlockSpXVx block(dom);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            block(ii, jj) = 1. * ii + .001 * jj;
        }
    }
    auto internal_mdspan = block.internal_mdspan();
    for (auto ii = block.ibegin<MeshX>(); ii < block.iend<MeshX>(); ++ii) {
        for (auto jj = block.ibegin<MeshVx>(); jj < block.iend<MeshVx>(); ++jj) {
            // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
            ASSERT_EQ(internal_mdspan(ii, jj), block(MCoordXVx(ii, jj)));
        }
    }
}

TEST_F(NonZeroDBlockXVxTest, slice)
{
    DBlockSpXVx block(dom);
    for (auto&& ii : block.domain<MeshX>()) {
        for (auto&& jj : block.domain<MeshVx>()) {
            block(ii, jj) = 1. * ii + .001 * jj;
        }
    }
    ASSERT_TRUE((std::is_same_v<
                 std::decay_t<decltype(block)>::layout_type,
                 std::experimental::layout_right>));
    {
        const DBlockSpXVx& constref_block = block;
        constexpr auto SLICE_VAL = 1;
        auto&& block_x = constref_block[MCoord<MeshVx>(SLICE_VAL)];
        ASSERT_TRUE((std::is_same_v<
                     std::decay_t<decltype(block_x)>::layout_type,
                     std::experimental::layout_stride>));
        ASSERT_EQ(block_x.extent<MeshX>(), block.extent<MeshX>());
        for (auto&& ii : constref_block.domain<MeshX>()) {
            // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
            ASSERT_EQ(block_x(ii), constref_block(ii, MCoord<MeshVx>(SLICE_VAL)));
        }

        auto&& block_v = constref_block[MCoord<MeshX>(SLICE_VAL)];
        ASSERT_TRUE((std::is_same_v<
                     std::decay_t<decltype(block_v)>::layout_type,
                     std::experimental::layout_right>));
        ASSERT_EQ(block_v.extent<MeshVx>(), block.extent<MeshVx>());
        for (auto&& ii : constref_block.domain<MeshVx>()) {
            // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
            ASSERT_EQ(block_v(ii), constref_block(MCoord<MeshX>(SLICE_VAL), ii));
        }

        auto&& subblock = constref_block[ProductMDomain<MeshX>(mesh_x, MCoordX(110), MLengthX(41))];
        // ASSERT_TRUE((std::is_same_v<
        //              std::decay_t<decltype(subblock)>::layout_type,
        //              std::experimental::layout_right>));
        ASSERT_EQ(subblock.extent<MeshX>(), 41);
        ASSERT_EQ(subblock.extent<MeshVx>(), select<MeshVx>(block.domain()).size());
        for (auto&& ii : subblock.domain<MeshX>()) {
            for (auto&& jj : subblock.domain<MeshVx>()) {
                // we expect complete equality, not ASSERT_DOUBLE_EQ: these are copy
                ASSERT_EQ(subblock(ii, jj), constref_block(ii, jj));
            }
        }
    }
}
