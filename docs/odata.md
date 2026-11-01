# OData support and roadmap

YarDB implements a practical **subset** of OData 4.x for document collections over HTTP. This page is the checklist of what works today vs what is planned. Runtime details and examples for shipped options live in [programs.md](programs.md#odata-query-parameters).

## Supported today

### Query options

| Option | Notes |
|--------|--------|
| `$top` / `$skip` | Client-driven paging. `$skip` still walks the rows it passes. A request that uses only `$skip` is unchanged |
| `$skiptoken` | Server-driven paging. The body stays a JSON array; there is no `@odata.nextLink` in the body. When `$top` is set and another row exists, the response carries `Link: </collection?$skiptoken=...>; rel="next"`. The link keeps `$filter`, `$orderby`, `$top`, and `$select`, replaces a previous `$skiptoken`, and drops `$skip`. `$desc=true` is kept when the request sent it. The token is an opaque base64url keyset of the last row returned, not an offset. With no `$orderby` it is that row's `_id` and the next read starts at the following primary key (`$desc` reverses the walk and does not read the token row or the rows already passed). With `$orderby` it is that row's sort values, in listed order, plus `_id`; the next page is the rows strictly after that tuple. `$skip` with `$skiptoken` is 422. A malformed token, or a token whose sort is not this request's `$orderby` and direction, is 422. Omitting `$top` returns every match and no link |
| `$orderby` | Comma-separated list, up to 8 top-level fields. Each field has its own `asc` or `desc`; a missing direction is ascending. Examples: `$orderby=settlementDate,tradeDate,creationDateTime,UID` and `$orderby=country,age desc`. A missing or null value sorts after a present one. Sort runs after `$filter` and before `$skip`/`$top` |
| `$filter` | See below |
| `$select` | Projection; `_id` always included |
| `$count=true` | Returns a JSON number; works with `$filter` |
| `$batch` | `POST /$batch` atomicity groups, independent writes, GET reads outside a group, and `dependsOn`; see [programs.md](programs.md#atomic-batch-post-batch) |
| `$expand` | v1: `{singular}_id` → plural collection `_id` |
| `$compute` | `Price mul Qty as LineTotal` (also `add`/`sub`/`div`); top-level numeric fields; runs after expand, before `$filter` / `$select` / paging (aliases usable in `$filter` / `$orderby`) |
| `$apply` | Pipeline of slash-separated transforms: `filter(...)`, `compute(...)`, `groupby((field),aggregate(...))`, `aggregate(...)`; methods `sum`/`average`/`min`/`max` |

### `$filter`

| Feature | Notes |
|---------|--------|
| Comparisons | `eq`, `ne`, `gt`, `ge`, `lt`, `le` |
| Logic | `not`, `and`, `or` (precedence: `not` > `and` > `or`); `not` is `!match(P)` (not `$ne`/`$lte` rewrites); nested/double `not` restores positive comparison branches; expressions that expand to more than 64 OR branches, nest more than 64 `not` operators, or exceed 8192 characters are rejected |
| `in` | Strings or numbers |
| Nested paths | `Customer/Country eq 'USA'` walks objects. A repeated group needs `any` |
| `any` | Post-filter, after the index read and before `$orderby`/`$skip`/`$top`. `Items/any(d:d/Quantity gt 100)` and `options/any(o: o/securitiesMovements/any(m: m/instrument eq 'FI0009000681'))`. `any()` is true only for a non-empty array. A missing path, an empty array, or a non-array is not a match. More than 8 nested lambdas is rejected. `all` is not supported |
| `startswith` | Index-backed on top-level secondary keys (not when negated) |
| `contains` / `endswith` | Post-filter only; `not contains` / `not endswith` supported |

### Metadata & format

| Feature | Notes |
|---------|--------|
| `GET /$metadata` | OData 4.01 **JSON** CSDL |
| Accept metadata | `odata=minimalmetadata` / `fullmetadata` / `nometadata` |
| REST keys | `/collection/id` (not `collection(id)` URL convention) |

## In the pipeline

Tracked here and in [development.md](development.md#development-roadmap). Order is preference, not a hard schedule.

### Near-term (next)

| Feature | Example | Status |
|---------|---------|--------|
| Multi-key `$apply` groupby | `groupby((country,status),aggregate(...))` | Planned |
| `$count` aggregate | `aggregate($count as Total)` | Planned |

### Later

| Feature | Example |
|---------|---------|
| Deeper `$expand` | `$expand=customer($select=name)`, nested expand, multi-valued refs |
| `$search` | `$search=wireless` |
| `all` | `Lines/all(l:l/Qty gt 0)` — true for an empty collection, so it is not `any` |
| Richer string/date functions | `indexof`, `tolower`, `year(...)` |
| Delta queries | `$deltaToken` |
| Bound actions / functions | `POST /users(1)/Default.ResetPassword` |
| XML CSDL `$metadata` | Beyond JSON CSDL |

## Design notes for `$apply`

- Slash-separated transforms outside quotes/parens: `filter(...)/compute(...)/groupby(...)`.
- Sibling query `$filter` runs **after** `$apply` (OData Aggregation evaluation order), so aggregate aliases like `Total` are filterable. Pre-aggregation narrowing uses a pipeline `filter(...)` step.
- `compute(...)` inside `$apply` uses the same arithmetic as query `$compute`.
- Query `$compute` cannot be combined with `$apply` (**422**) — use `compute(...)` in the pipeline.
- `$apply` does **not** compose with `$expand`, `$select`, `$orderby`, `$top`, `$skip`, or `$count` (rejected with **422**).
- Response for aggregate/groupby is a JSON **array** of group objects, e.g. `[{"status":"active","Total":42},…]`.
- Group keys and compute operands are top-level fields only (no `Customer/Country` yet).
- Aggregate methods: `sum`, `average`, `min`, `max`. Compute ops: `add`, `sub`, `mul`, `div`.
- Compute/aggregate aliases cannot be `_id` (**422**). `filter(...)` / `compute(...)` / `aggregate(...)` / `groupby(...)` require a balanced closing `)` with no trailing junk.

```bash
curl -s "http://127.0.0.1:2112/orders?\$apply=groupby((status),aggregate(amount%20with%20sum%20as%20Total))"
curl -s "http://127.0.0.1:2112/orders?\$apply=filter(status%20eq%20'active')/groupby((country),aggregate(amount%20with%20sum%20as%20Total))"
curl -s "http://127.0.0.1:2112/lines?\$compute=Price%20mul%20Qty%20as%20LineTotal&\$select=LineTotal"
```

Tests: `./tools/CB.sh debug test "groupby" --jsonl=failures` and `./tools/CB.sh debug test "compute" --jsonl=failures`.
