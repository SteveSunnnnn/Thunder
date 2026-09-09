"""Resolve freshly authored V3-reference rules onto public GIS identifiers."""
import argparse
import json
from pathlib import Path
import geopandas as gpd


def build(rules, locations, admin1):
    regions = dict(zip(admin1.adm1_code, admin1.region))
    result = {"schema_version": 1, "scenario_year": 1836, "default_policy": "unassigned",
              "status": rules["status"], "reference_review": rules["reference"],
              "known_geometry_gaps": rules["known_geometry_gaps"],
              "countries": rules["countries"], "admin0_owners": {}, "admin1_owners": {},
              "explicit_unassigned": [], "matched_rules": []}
    result["admin0_owners"] = {k: "country." + v for k, v in rules["territory_owners"].items()}
    for rule in rules["rules"]:
        for reference in rule.get("v3_state", "").split(";"):
            allowed = rules.get("reference_scopes", {}).get(reference)
            if allowed and rule["territory"] not in allowed:
                raise ValueError(f"reference scope mismatch: {reference} cannot identify {rule['territory']}")
        group = locations[locations.territory_code == rule["territory"]]
        if "names" in rule:
            group = group[group["name"].isin(rule["names"])]
        else:
            group = group[group.source_id.map(regions).isin(rule["regions"])]
        if group.empty:
            raise ValueError(f"scenario rule matches no public geometry: {rule}")
        for key in group.source_id:
            result["admin1_owners"][key] = "country." + rule["owner"] if rule["owner"] else ""
        result["matched_rules"].append({**rule, "source_ids": list(group.source_id)})
    result["explicit_unassigned"] = [k for k, v in result["admin1_owners"].items() if not v]
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rules", type=Path, default=Path("data/geography/scenario_1836_rules.json"))
    parser.add_argument("--source", type=Path, default=Path("generated/world_map/source/locations.gpkg"))
    parser.add_argument("--admin1", type=Path, default=Path("data/geography/raw/ne_10m_admin_1_states_provinces.zip"))
    parser.add_argument("--out", type=Path, default=Path("data/geography/ownership_1836.json"))
    args = parser.parse_args()
    result = build(json.loads(args.rules.read_text(encoding="utf-8")), gpd.read_file(args.source),
                   gpd.read_file(f"zip://{args.admin1}"))
    args.out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"V3-only authored rules: {len(result['matched_rules'])}; leaf overrides: {len(result['admin1_owners'])}")


if __name__ == "__main__":
    main()
