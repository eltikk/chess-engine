#include "MagicBitboards.h"
#include "MoveGenerator.h"
#include "TestUtils.h"

int main() {
    // Shared lookup tables are initialized once before any suite runs.
    MagicBitboards::init();
    MoveGenerator::init();

    return Test::runAllTests();
}
