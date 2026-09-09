# Finance, Foreign Exchange, Trade, and Banking

Thunder 1.0 keeps domestic nominal accounts in each market's currency and uses
`thunder.currency.default` only as the international numeraire. Fixed-point
integer arithmetic is authoritative; floating point is not used for ledger
settlement.

## Currency and foreign exchange

`CurrencyStore` supports gold, silver, bimetallic, and floating-fiat standards.
Cross-currency invoices use `amount_to = amount_from * rate_from / rate_to`.
Trade pressure adjusts floating rates by a bounded weekly step. Convertible
metallic currencies are defended inside physical shipping points; reserve
exhaustion suspends convertibility. Country foreign reserves and balance of
payments are always stored in the global numeraire, never as mixed local
invoice amounts.

**FX settlement is dual-path** (design: `audit/2026-09-05_monetary_standard_fx_design.md`):

- the unit-of-account path above (`convert` / `convert_price`); and
- the commodity path `coin_commodity_value_milli(coin, gold_price_ppm,
  silver_price_ppm)` — what a foreign coin is worth as metal (melt value from
  its mint parity x the market metal price). Gold/Silver standards use their
  own metal; bimetallism takes the richer leg (melt side of Gresham's law —
  the circulation rate tracks the cheaper leg); fiat is 0 and settles purely
  at the FX rate. The spread between the two paths is the arbitrage space the
  existing specie-flow mechanism closes.

Monetary metals are ordinary market goods on the goods side:
`good gold_bullion { category = currency monetary_metal = gold }` — their market
price is the metal price that feeds `update_exchange_rates` every weekly tick
(numeraire-averaged across markets, normalized by base price; the classical
1.0/15.5 constants remain only as the no-bullion fallback). A gold rush that
floods markets with bullion depreciates the anchor and drags metallic-standard
parities with it; scarcity appreciates them.

The weekly monetary audit separates five effects:

- transfers, which must net to zero;
- private bank credit creation or contraction;
- central issuance represented by seigniorage;
- numeraire revaluation caused by exchange-rate movement;
- inventory carrying cost (0.1%/week of settled inventory value, booked
  against market clearing as the finance sink for idle stock).

`unexplained_monetary_delta_milli` is the remaining accounting residual after
those effects. Small integer rounding residuals are expected; persistent or
large values are a regression.

## Trade settlement

When authored `TradeRouteRecord` entries exist, trade is settled in stable
route-ID order and capped by route quantity, route level, exporter inventory,
importer shortage, and both countries' logistics capacity. Import and export
tariffs enter the landed-price test and are transferred to the corresponding
treasury. With no authored routes, the compatibility open-market mode performs
deterministic price arbitrage using prices normalized to the global numeraire.

The authored path is `O(R)` per week. Open-market fallback is approximately
`O(G * (M log M + P))`, where `P` is the number of profitable importer/exporter
pairs actually examined. No POP is visited by the trade phase.

## Commercial banks

`BankStore` uses SoA columns for banks and loan contracts. Every bank and loan
has a non-zero stable key. A bank records reserves, customer deposits, equity,
building loans, sovereign bonds, non-performing exposure, retained earnings,
regulatory ratios, rates, and operating status.

Loan origination creates equal loan assets and customer deposit liabilities.
The funded amount is capped by both:

- `reserves / deposits >= reserve_requirement`; and
- `equity / risk_assets >= capital_requirement`.

Weekly servicing runs in stable loan-ID order. Payments reduce borrower cash
and bank deposits; principal reduces the loan asset; interest raises bank
equity. Four missed payments mark a loan non-performing. Twelve missed payments
charge the remaining principal against equity. Banks with negative equity are
insolvent and cannot lend. Sovereign bond proceeds must be funded by regulated bank capacity or
accumulated investment-pool cash; unfunded bond requests no longer create
treasury money. **Restructuring:** a month of missed weekly service writes the
debt down 30% — domestic banks absorb their pro-rata share through equity
(possibly going insolvent, which is the bank-crisis channel), the saver pool
absorbs the rest, the rating drops to D and the exclusion clock resets to 52
weeks; each fully serviced week counts it back down.

Bank processing is `O(B + L)` per week and is deliberately serial because it is
low-cardinality ledger work. Country/currency and stable-key indexes make bank
selection `O(log B)`. It does not enter the market-parallel POP/building hot
scans.

## Persistence and scripting

The optional `FIN1` save section stores trade policy, logistics usage, bank
balance sheets, and loan contracts. It is validated before atomic world commit.
All fields participate in the deterministic world checksum; saves written
before `FIN1` migrate to empty banking and default trade policy state.

ThunderScript exposes tariff and logistics policy effects, sovereign issue/repay
effects, primary-currency selection, and bank reserve/lending-capacity triggers.
Initial bank capitalization remains content/bootstrap work because it requires
a complete opening balance sheet rather than an unconstrained runtime effect.

## Current boundaries

- Interbank payment networks, lender-of-last-resort facilities, deposit
  insurance, and bank mergers are not modeled yet.
- Foreign holders of sovereign bonds are not individually identified; bank-held
  domestic bonds and the aggregate domestic saver pool receive settlement.
- Trade routes do not yet distinguish land, sea, or river transport modes;
  logistics capacity is a generic international capacity budget.
- Derivatives, equity exchanges, and intraday FX order books are intentionally
  outside the weekly historical-strategy simulation layer.
