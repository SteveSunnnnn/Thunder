"""Deterministic geometry and distance-field regressions (synthetic, not history)."""
import unittest
import numpy as np
import geopandas as gpd
from shapely.geometry import box, Point, Polygon
from shapely.ops import unary_union

from constrained_topology import (constrain_locations, bind_historical_settlements,
    shared_edge_document, smooth_shared_boundaries, consolidate_strategic_regions)
from coast_distance import coast_distance_jfa
from prepare_world_geography import owner_for
from build_scenario_rules import build as build_scenario
from bake_cartography import signed_chart_distance
from refine_wiki_geography import partition, ocean_coast_flags, split_control_regions
from compile_scenario import load_location_overrides
from thunder_gis_compile import _shared_border_length, _representative_point
import json
from pathlib import Path
import tempfile


def frame(geometries, **columns):
    return gpd.GeoDataFrame(columns, geometry=geometries, crs=3857)


class GeometryTests(unittest.TestCase):
    def setUp(self):
        self.domain = box(0, 0, 1000, 1000)
        self.coast = frame([self.domain])
        self.locations = frame([self.domain], province_key=["seed"], state_key=["state"], area_key=["area"])
        self.modern = frame([box(0, 0, 500, 1000), box(500, 0, 1000, 1000)], sovereign_key=["a", "b"])
        self.history = frame([box(0, 0, 1000, 500), box(0, 500, 1000, 1000)], sovereign_key=["h1", "h2"], year=[1836, 1836])

    def compile(self, **kwargs):
        return constrain_locations(self.locations, self.coast, kwargs.get("lakes"), self.modern, self.history)

    def test_crossing_borders_form_four_whole_atoms(self):
        result = self.compile()
        self.assertEqual(len(result), 4)
        self.assertEqual(set(result.constraint_flags), {3})
        self.assertEqual(unary_union(result.geometry).symmetric_difference(self.domain).area, 0)
        for _, row in result.iterrows():
            self.assertIn(row.geometry.bounds[0], (0, 500))
            self.assertIn(row.geometry.bounds[1], (0, 500))
            self.assertEqual(row.country_tag, row.sovereign_1836)

    def test_lake_is_excluded_exactly(self):
        lake = box(100, 100, 300, 300)
        result = self.compile(lakes=frame([lake]))
        self.assertEqual(unary_union(result.geometry).intersection(lake).area, 0)
        self.assertEqual(result.geometry.area.sum(), self.domain.area - lake.area)

    def test_missing_history_rejected(self):
        self.history = self.history.iloc[:1]
        with self.assertRaisesRegex(ValueError, "missing coverage"):
            self.compile()

    def test_wrong_history_year_rejected(self):
        self.history["year"] = 1886
        with self.assertRaisesRegex(ValueError, "1836"):
            self.compile()

    def test_overlapping_polities_rejected(self):
        self.modern = frame([self.domain, box(400, 0, 1000, 1000)], sovereign_key=["a", "b"])
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.compile()

    def test_shared_edge_manifold(self):
        doc = shared_edge_document(self.compile())
        self.assertTrue(any(len(owners) == 2 for owners in doc["edge_locations"]))
        self.assertTrue(all(len(owners) in (1, 2) for owners in doc["edge_locations"]))
        self.assertEqual(len(doc["edges"]), len({str(e) for e in doc["edges"]}))

    def test_settlements_are_not_geometric_centers(self):
        locations = self.compile()
        points = frame([Point(g.bounds[0] + 80, g.bounds[1] + 90) for g in locations.geometry],
                       ascii_key=[f"loc_test_town_{i}" for i in range(4)],
                       name=["Town"] * 4, year=[1836] * 4, elevation_m=[10] * 4)
        result = bind_historical_settlements(locations, points)
        self.assertTrue((result.settlement_x_m != result.geometric_center_x_m).all())
        self.assertTrue(result.location_key.is_unique)

    def test_missing_town_is_not_fabricated(self):
        points = frame([Point(80, 80)], ascii_key=["loc_test_town"], name=["Town"], year=[1836], elevation_m=[10])
        with self.assertRaisesRegex(ValueError, "no evidenced"):
            bind_historical_settlements(self.compile(), points)

    def test_shared_smoothing_preserves_coast_and_changes_staircase(self):
        left = Polygon([(0, 0), (500, 0), (500, 300), (600, 300),
                        (600, 500), (400, 500), (400, 700), (500, 700), (500, 1000), (0, 1000)])
        locs = frame([left, self.domain.difference(left)], modern_sovereign=["a", "a"], sovereign_1836=["h", "h"])
        smooth = smooth_shared_boundaries(locs, self.domain.boundary)
        self.assertGreater(smooth.geometry.iloc[0].symmetric_difference(left).area, 0)
        self.assertEqual(smooth.geometry.iloc[0].intersection(smooth.geometry.iloc[1]).area, 0)
        self.assertEqual(unary_union(smooth.geometry).symmetric_difference(self.domain).area, 0)

    def test_locked_borders_do_not_move(self):
        original = self.compile()
        locked = unary_union([g.boundary for g in original.geometry])
        smooth = smooth_shared_boundaries(original, locked)
        for a, b in zip(original.geometry, smooth.geometry):
            self.assertEqual(a.symmetric_difference(b).area, 0)

    def test_regional_compile_does_not_require_unrelated_microstates(self):
        original = self.compile()
        original["territory_code"] = "TEST"
        self.assertEqual(len(consolidate_strategic_regions(original)), 4)
        with self.assertRaisesRegex(ValueError, "required single-Location"):
            consolidate_strategic_regions(original, require_special_states=True)


class DistanceTests(unittest.TestCase):
    def test_chart_distance_retains_range_beyond_physical_coast(self):
        mask = np.zeros((32, 128), dtype=bool)
        mask[:, :16] = True
        chart = signed_chart_distance(mask, 8000.0, wrap=False)
        self.assertAlmostEqual(float(chart[16, 64]) * 32.0, -388000.0, delta=16.0)

    def test_chart_distance_wraps_at_dateline(self):
        mask = np.zeros((8, 64), dtype=bool)
        mask[:, 0] = True
        chart = signed_chart_distance(mask, 8000.0)
        self.assertEqual(chart[4, 1], chart[4, 63])

    def test_straight_coast_half_pixel_seeds(self):
        mask = np.zeros((16, 16), bool)
        mask[:, 8:] = True
        distance = coast_distance_jfa(mask, 2.0)
        np.testing.assert_allclose(distance, np.tile((np.arange(16) - 7.5) * 2, (16, 1)))

    def test_uniform_pages_saturate_without_fake_coast(self):
        for land in (False, True):
            distance = coast_distance_jfa(np.full((128, 128), land), 500)
            np.testing.assert_array_equal(distance, 16383.5 if land else -16383.5)

    def test_halo_pages_agree_with_whole_field(self):
        mask = np.indices((160, 256))[1] >= 127
        full = coast_distance_jfa(mask, 10)
        tile = coast_distance_jfa(mask[:, 96:160], 10)
        np.testing.assert_allclose(tile[:, 10:-10], full[:, 106:150])


class ScenarioTests(unittest.TestCase):
    def test_location_override_rejects_stale_identity_and_preserves_unassigned(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'overrides.json'
            path.write_text(json.dumps({'schema_version':1, 'scenario_year':1836,
                                        'owners': {'split': ''}}))
            self.assertEqual(load_location_overrides(path, ['split']), {'split': ''})
            with self.assertRaisesRegex(ValueError, 'missing source identities'):
                load_location_overrides(path, ['old_parent'])

    def test_same_name_on_another_continent_is_rejected(self):
        locations = frame([box(0, 0, 1, 1)], territory_code=["ARG"], source_id=["arg1"], name=["Formosa"])
        admin = frame([box(0, 0, 1, 1)], adm1_code=["arg1"], region=["Formosa"])
        rules = {"status": "test", "reference": {}, "known_geometry_gaps": [], "countries": {},
                 "territory_owners": {}, "reference_scopes": {"STATE_FORMOSA": ["TWN"]},
                 "rules": [{"territory": "ARG", "names": ["Formosa"], "owner": "chi", "v3_state": "STATE_FORMOSA"}]}
        with self.assertRaisesRegex(ValueError, "reference scope mismatch"):
            build_scenario(rules, locations, admin)

    def test_unresolved_split_never_inherits_modern_territory_owner(self):
        scenario = {"default_policy": "unassigned", "admin0_owners": {"RUS": "country.rus"},
                    "admin1_owners": {"mixed": ""}, "explicit_unassigned": ["mixed"]}
        self.assertEqual(owner_for(scenario, "RUS", "mixed"), "")
        self.assertEqual(owner_for(scenario, "UNKNOWN", "missing"), "")

    def test_rule_build_uses_only_declared_reference_assignments(self):
        locations = frame([box(0, 0, 1, 1)], territory_code=["X"], source_id=["x1"], name=["Town"])
        admin = frame([box(0, 0, 1, 1)], adm1_code=["x1"], region=["Region"])
        rules = {"status": "test", "reference": {}, "known_geometry_gaps": [], "countries": {},
                 "territory_owners": {"X": "initial"},
                 "rules": [{"territory": "X", "names": ["Town"], "owner": ""}]}
        result = build_scenario(rules, locations, admin)
        self.assertEqual(owner_for(result, "X", "x1"), "")
        self.assertEqual(set(result["admin0_owners"]), {"X"})


class RefinementTests(unittest.TestCase):
    def test_territory_controls_cover_the_district_without_overlap(self):
        domain=box(0,0,1000,1000)
        controls=[{'key':'a','owner':'pru','lon':.002,'lat':.004},
                  {'key':'b','owner':'anh','lon':.007,'lat':.004}]
        cells=split_control_regions(domain,controls)
        self.assertLess(unary_union(cells).symmetric_difference(domain).area,.001)
        self.assertLess(cells[0].intersection(cells[1]).area,.001)
        self.assertTrue(all(cell.area>0 for cell in cells))

    def test_territory_controls_cannot_introduce_an_ownerless_child(self):
        with self.assertRaisesRegex(ValueError,'explicit owners'):
            split_control_regions(box(0,0,1000,1000),
                                  [{'key':'a','owner':'','lon':.002,'lat':.004}])

    def test_interior_focus_does_not_fall_in_a_hole(self):
        geometry = box(0,0,20000,20000).difference(box(5000,5000,15000,15000))
        row = frame([geometry],representative_method=['interior']).iloc[0]
        point = _representative_point(row)
        self.assertTrue(geometry.covers(point))
        self.assertGreater(point.distance(geometry.boundary),2000)

    def test_children_recompute_coast_instead_of_inheriting_parent_flag(self):
        cells = [box(0,0,50,50), box(0,50,50,100), box(100,0,150,50)]
        coast = box(0,-100,100,0).boundary
        self.assertEqual(ocean_coast_flags(cells, coast), [True, False, False])

    def test_boundary_only_district_is_not_a_land_location(self):
        with self.assertRaisesRegex(ValueError, 'Location collapsed'):
            partition(box(0,0,10,10),frame([box(10,0,20,10)],shapeName=['A']),
                      [{'key':'a','districts':['A']}])

    def test_adjacency_survives_projection_slivers_but_not_real_gaps(self):
        a = box(0,0,1000,1000)
        for delta in [-1e-7, 1e-7]:
            self.assertGreater(_shared_border_length(a, box(1000+delta,0,2000,1000)), 999)
        self.assertEqual(_shared_border_length(a, box(1001,0,2000,1000)), 0)
        self.assertEqual(_shared_border_length(a, box(500,0,1500,1000)), 0)
        self.assertLess(_shared_border_length(a, box(1000,1000,2000,2000)), 1)
        # A large parent's perimeter must not make a contained, overlapping
        # residual polygon look like a valid neighbouring territory.
        self.assertEqual(_shared_border_length(box(0,0,1e6,1e6),box(0,0,100,100)),0)

    def test_duplicate_district_records_preserve_mainland_and_island(self):
        districts = frame([box(0,0,4,10), box(8,0,10,2), box(4,0,10,10)],
                          shapeName=['A', 'A', 'B'])
        cells = partition(box(0,0,10,10), districts,
                          [{'key':'a','districts':['A']}, {'key':'b','districts':['B']}])
        self.assertEqual(cells[0].area, 44)
        self.assertEqual(cells[1].area, 56)
        self.assertEqual(unary_union(cells).area, 100)
        self.assertEqual(cells[0].intersection(cells[1]).area, 0)

    def test_source_gap_and_outside_land_do_not_change_parent_coverage(self):
        districts = frame([box(-2,0,4,10),box(5,0,12,10)],shapeName=['A','B'])
        domain = box(0,0,10,10)
        cells = partition(domain,districts,[{'key':'a','districts':['A']}, {'key':'b','districts':['B']}])
        self.assertEqual(unary_union(cells).symmetric_difference(domain).area, 0)
        self.assertEqual(cells[0].intersection(cells[1]).area, 0)

    def test_duplicate_assignment_fails_before_emitting_cells(self):
        with self.assertRaisesRegex(ValueError, 'more than one Location'):
            partition(box(0,0,10,10),frame([box(0,0,10,10)],shapeName=['A']),
                      [{'key':'a','districts':['A']}, {'key':'b','districts':['A']}])


if __name__ == "__main__":
    unittest.main()
