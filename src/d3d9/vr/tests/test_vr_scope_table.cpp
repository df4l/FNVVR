#include "test.h"

#include "../vr_scope_table.h"

using namespace dxvk;

namespace {

  constexpr float Tolerance = 1e-4f;

}


TEST_CASE(scope_table_knows_the_games_scopes_in_any_case) {
  VrScopeTable table;
  VrScopePlacement placement;

  CHECK(table.find("Weapons\\2HandRifle\\SniperRifle.NIF", placement));
  CHECK_NEAR(placement.eyepiece.y, 11.843f, Tolerance);
  CHECK(placement.hasLens());
  CHECK(!placement.rectangular);

  CHECK(table.find("weapons\\2handrifle\\laserrifle.nif", placement));
  CHECK(placement.rectangular);
  CHECK(placement.halfWidth > placement.halfHeight);

  CHECK(!table.find("weapons\\2handrifle\\unknown.nif", placement));
  CHECK(!table.find(nullptr, placement));
}


TEST_CASE(scope_table_has_models_with_nothing_to_look_through) {
  VrScopeTable table;
  VrScopePlacement placement;

  CHECK(table.find("weapons\\1handmelee\\binoculars.nif", placement));
  CHECK(!placement.hasLens());
}


TEST_CASE(scope_table_corrections_take_precedence) {
  VrScopeTable table;

  size_t count = table.parse(
    "; comment\n"
    "\n"
    "Weapons\\Mine.NIF = 1 2 3 0.5\r\n"
    "weapons\\flat.nif = 1 2 3 0.5 0.25\n"
    "weapons\\2handrifle\\sniperrifle.nif = 4 5 6 1.5\n"
    "weapons\\2handrifle\\marksman.nif = none\n"
    "weapons\\bad.nif = 1 2\n"
    "weapons\\zero.nif = 1 2 3 0\n"
    "no separator\n");

  CHECK(count == 4);

  VrScopePlacement placement;

  CHECK(table.find("weapons\\mine.nif", placement));
  CHECK_NEAR(placement.eyepiece.z, 3.0f, Tolerance);
  CHECK_NEAR(placement.halfHeight, 0.5f, Tolerance);
  CHECK(!placement.rectangular);

  CHECK(table.find("weapons\\flat.nif", placement));
  CHECK(placement.rectangular);
  CHECK_NEAR(placement.halfHeight, 0.25f, Tolerance);

  CHECK(table.find("weapons\\2handrifle\\sniperrifle.nif", placement));
  CHECK_NEAR(placement.eyepiece.x, 4.0f, Tolerance);

  CHECK(table.find("weapons\\2handrifle\\marksman.nif", placement));
  CHECK(!placement.hasLens());

  CHECK(!table.find("weapons\\bad.nif", placement));
  CHECK(!table.find("weapons\\zero.nif", placement));
}


TEST_CASE(scope_table_guesses_above_the_barrel) {
  VrScopePlacement placement = VrScopeTable::guess({ 50.0f, 6.0f, 0.5f });

  CHECK(placement.hasLens());
  CHECK_NEAR(placement.eyepiece.y, 10.5f, Tolerance);
  CHECK_NEAR(placement.eyepiece.z, 0.5f, Tolerance);
}
