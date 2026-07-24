#include "TestSupport.hpp"

#include "nav/NavMeshBuilder.hpp"

using world::nav::CellRect;
using world::nav::neighbourCellRect;

int main()
{
    CellRect rect{};

    CHECK(neighbourCellRect(-1, 0, rect));
    CHECK(rect.ixFirst == 127 && rect.ixLast == 127);
    CHECK(rect.iyFirst == 0 && rect.iyLast == 127);

    CHECK(neighbourCellRect(1, 0, rect));
    CHECK(rect.ixFirst == 0 && rect.ixLast == 0);
    CHECK(rect.iyFirst == 0 && rect.iyLast == 127);

    CHECK(neighbourCellRect(0, -1, rect));
    CHECK(rect.ixFirst == 0 && rect.ixLast == 127);
    CHECK(rect.iyFirst == 127 && rect.iyLast == 127);

    CHECK(neighbourCellRect(0, 1, rect));
    CHECK(rect.ixFirst == 0 && rect.ixLast == 127);
    CHECK(rect.iyFirst == 0 && rect.iyLast == 0);

    CHECK(!neighbourCellRect(0, 0, rect));
    CHECK(!neighbourCellRect(1, 1, rect));
    CHECK(!neighbourCellRect(-2, 0, rect));

    return mangos::test::failures == 0 ? 0 : 1;
}
