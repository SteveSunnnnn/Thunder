# Thunder 1.0 Economy Architecture

## Goal

Thunder 1.0 provides a scalable POP/building/market loop with deterministic results
across worker counts. Currency, routed trade, banking, credit, ownership,
construction, qualifications, and migration layer onto the same data-oriented
world without entering unrelated hot paths.

## Runtime data layout

Mutable high-cardinality data is stored as SoA columns:

- `PopStore`: market, population, employed population, employer, need profile, income, standard of living.
- `BuildingStore`: market, building type, level, employees, wage offer, cash, last profit.
- `MarketStore`: owner country plus flat `[market][good]` price/supply/demand arrays.
- `MarketEntityIndex`: immutable/rebuilt CSR-style market -> POP/building ID lists.

Cold recipe names and content keys live in `EconomyDefinitions`; the weekly hot path uses compact typed
IDs and flat recipe/need-flow arrays.

## Deterministic fixed point

Market quantities, prices and money use signed 64-bit integer fixed point. Values are expressed in
milli-units where appropriate. This avoids floating-point summation order becoming simulation state.
Country aggregate values still cross the legacy `CountryStore` double boundary once per country after a
stable market-order fold; the high-cardinality market loop itself is integer.

## Parallel ownership model

The primary parallel unit is a market, grouped into fixed four-market jobs. A job owns all writes to its
market's supply/demand/price row and to entities indexed under that market. This avoids atomics in the
normal economic path.

Weekly phases:

1. Employment/capacity reconciliation. Wages are settled here, per hire segment:
   each building is debited for exactly the workers it hired at its own offer
   (clamped by its cash plus credit line), and the POP's weekly income is the
   exact segment sum. Hiring is province-local by design decision (no commuting).
2. Building production and industrial input demand.
3. POP demand aggregation by `(market, need_profile)` from settled incomes.
4. Market price convergence. Settled inventory pays 0.1%/week carrying cost
   into market clearing (the audited finance sink for idle stock).
5. Building settlement. Output/input money settles through the state's
   market-access wedge (up to ±25% at zero access). Dividends split by
   ownership stakes: the company-owned share goes to company operating cash,
   the rest to the national investment pool. Buildings pinned at the credit
   limit while operating at a loss are collected for bankruptcy resolution.
6. Serial fold: country aggregation, sovereign debt service (with
   restructuring after a month of missed payments), bankruptcy execution
   (multi-level buildings shrink a level per loss week; one-level buildings
   die only on demonstrably failed bank debt), FX revaluation from
   market-driven metal prices, then construction (queued projects consume real
   materials from their market plus money; investment-pool and company
   expansions enter the queue as escrowed bills instead of appearing
   instantly).

Country treasury/GDP/population aggregation is folded in market ID order after parallel settlement so
multiple markets owned by one country cannot become scheduling-order dependent.

## Hot-path optimizations

### Column views

Public entity APIs retain checked typed-ID accessors, but EconomySystem validates the world layout once
then holds `std::span` column views inside a phase. This removed millions of repeated bounds checks and
function calls in the 300k-POP benchmark.

### Direct employer capacity index

Employment does not search a market's buildings for every POP. `BuildingId` indexes a reusable
`building_remaining_` array directly. Market ownership guarantees no two market jobs mutate the same
building slot.

### Need-profile aggregation

POP consumption does not traverse an identical goods basket for every cohort. It first sums population
by `(market, need_profile)`, then evaluates each profile's goods flows once per market. Settlement also
precomputes the current basket cost per `(market, need_profile)` before scanning POPs.

This is especially important because realistic worlds may have hundreds of thousands of POP cohorts but
only a small number of need profiles.

## Current model boundaries

Thunder 1.0 deliberately keeps several systems simple:

- Employment is re-solved weekly: buildings compete by wage offer for the market's
  POPs (province-local candidates first, unassigned-pool fallback), and a POP may
  split across several employers. Per standing design decision, hiring is
  province-local: there is no cross-province commuting (R9 reviewed and rejected).
- Companies are shell owners no longer: ownership stakes divert the owned share
  of building dividends into company cash, and companies spend it on their own
  queued expansions.
- Migration preserves POP identity (merge into matching cohort or found a new
  one carrying culture/religion/profession, habits and savings), and an
  unemployment-driven weekly generator creates bounded domestic flows toward
  same-country vacancies.
- Markets use price response to supply/demand imbalance rather than stockpile logistics.
- Route transport modes and interbank/central-bank networks remain aggregate;
  see `FINANCE_BANKING.md` for implemented contracts and boundaries.
- Wage adjustment is a bounded weekly feedback rule, not the final bargaining model.
- Price history/ring buffers are not yet implemented.

These are content/simulation layers on top of the current stores, not reasons to replace the data layout.
