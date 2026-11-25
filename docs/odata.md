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
| `$expand` | `{singular}_id` → plural collection `_id`; else `{singular}_{UniqueKey}` such as `instrument_isin` → a unique secondary key on that collection. `$expand=instrument($select=symbol)` and `$expand=customer($select=name)` keep those properties on the nested object, and `_id` when it is not listed. An unknown option, including `$expand=instrument($isin=isin)`, or a bad `$select`, is 422 |
| `$compute` | `Price mul Qty as LineTotal` (also `add`/`sub`/`div`); top-level numeric fields; runs after expand, before `$filter` / `$select` / paging (aliases usable in `$filter` / `$orderby`) |
| `$apply` | Pipeline of slash-separated transforms: `filter(...)`, `compute(...)`, `groupby((field,...))`, `groupby((field,...),aggregate(...))`, `aggregate(...)`; methods `sum`/`average`/`min`/`max`, and `aggregate($count as Alias)`. `groupby` returns one row per distinct tuple of those top-level fields, including `groupby((country,status),aggregate(...))` |
| `$deltaToken` | Changes since a cursor: new and updated documents, plus deletes. A delete is an append-only record, not a rewritten row. The body is `{"value":[...],"deltaToken":"..."}`. A missing or empty token reads from the start and still returns a token. An unknown token is 400. The token is a file offset. Other query options on the same request are 422 |

### `$filter`

| Feature | Notes |
|---------|--------|
| Comparisons | `eq`, `ne`, `gt`, `ge`, `lt`, `le` |
| Logic | `not`, `and`, `or` (precedence: `not` > `and` > `or`); `not` is `!match(P)` (not `$ne`/`$lte` rewrites); nested/double `not` restores positive comparison branches; expressions that expand to more than 64 OR branches, nest more than 64 `not` operators, or exceed 8192 characters are rejected |
| `in` | Strings or numbers |
| Nested paths | `Customer/Country eq 'USA'` walks objects. A repeated group needs `any` or `all` |
| `any` | Post-filter, after the index read and before `$orderby`/`$skip`/`$top`. `Items/any(d:d/Quantity gt 100)` and `options/any(o: o/securitiesMovements/any(m: m/instrument eq 'FI0009000681'))`. `any()` is true only for a non-empty array. A missing path, an empty array, or a non-array is not a match. More than 8 nested lambdas is rejected |
| `all` | Same post-filter position as `any`. `lines/all(l: l/qty gt 0)` is true when every element matches. An empty array is true. A missing path or a non-array is not a match. `all()` is rejected. Nested `all` shares the 8-deep limit with `any` |
| `startswith` | Index-backed on top-level secondary keys (not when negated, and not when the field is wrapped in `tolower` / `toupper`) |
| `contains` / `endswith` | Post-filter only; comparison stays case-sensitive. `not contains` / `not endswith` supported |
| `tolower` / `toupper` | ASCII `A-Z` / `a-z` only. Case-insensitive matching is `contains(tolower(field), tolower('text'))` (also `startswith`, `endswith`, and `eq` / `ne` / `gt` / `ge` / `lt` / `le`). A folded field is a post-filter. A folded string literal is a constant, so `startswith(name, tolower('AB'))` can still use the prefix index |
| `indexof` | `indexof(field,'text')` is the zero-based index of the first match, or `-1`. Post-filter. Example: `indexof(name,'Bond') ge 0`. A missing field or a non-string compares false, including `eq -1` |
| `substring` | `substring(field,start)` and `substring(field,start,length)`, zero-based. Past the end is `''`. A negative start or length compares false. Example: `substring(isin,0,2) eq 'FI'` |
| `length` | `length(field)` is the string's length. Example: `length(name) gt 3`. A missing field or a non-string compares false |
| `year` / `month` / `day` | Calendar part of a date `YYYY-MM-DD`, an ISO-8601 timestamp (the date as written, with `Z` or a numeric offset), or a timestamp value (UTC). Example: `year(tradeDate) eq 2026`. A missing field or any other value compares false. Wrong arity or a non-field argument on these calls is **422** |

### Metadata & format

| Feature | Notes |
|---------|--------|
| `GET /$metadata` | OData 4.01 **JSON** CSDL |
| Accept metadata | `odata=minimalmetadata` / `fullmetadata` / `nometadata`. Under minimal or full, a GET names each entity's `@odata.etag` |
| REST keys | `/collection/id` (not `collection(id)` URL convention) |

## In the pipeline

Tracked here and in [development.md](development.md#development-roadmap). Order is preference, not a hard schedule.

### Near-term (next)

No items. Nested `$expand` and multi-valued refs remain under Later.

### Later

| Feature | Example |
|---------|---------|
| Deeper `$expand` | Nested expand, multi-valued refs (`$expand=instrument($select=symbol)` and unique-secondary expand such as `instrument_isin` are shipped) |
| `$search` | `$search=wireless` |
| Bound actions / functions | `POST /users(1)/Default.ResetPassword` |
| XML CSDL `$metadata` | Beyond JSON CSDL |

## Design notes for `$apply`

- Slash-separated transforms outside quotes/parens: `filter(...)/compute(...)/groupby(...)`.
- `groupby((field,...))` is supported (OData Data Aggregation 4.0 §3.2.3 and §7.1), including several grouping properties: `groupby((country,status))` and `groupby((country,status),aggregate(...))`. The second parameter of `groupby` is optional. Omitted, the result is one object per distinct tuple, and each object contains only those fields. A missing property is `null` in that column, so a missing `country` with `status` `active` is a different group from a present country; JSON `null` joins that same group. An object or array value in any grouping property is rejected (**422**). Names are top-level fields only. An empty name, a repeated name, or a transformation after the group list (including another pipeline step after `groupby`) is rejected. With an aggregate, each row is the grouping fields plus the aggregate alias.
- Sibling query `$filter` runs **after** `$apply` (OData Aggregation evaluation order), so aggregate aliases like `Total` are filterable. Pre-aggregation narrowing uses a pipeline `filter(...)` step.
- `compute(...)` inside `$apply` uses the same arithmetic as query `$compute`.
- Query `$compute` cannot be combined with `$apply` (**422**) — use `compute(...)` in the pipeline.
- `$apply` does **not** compose with `$expand`, `$select`, `$orderby`, `$top`, `$skip`, or `$count` (rejected with **422**).
- Response for aggregate/groupby is a JSON **array** of group objects, e.g. `[{"status":"active","Total":42},…]`, `[{"country":"FI","status":"active","Total":15}]`, or `[{"type":"sss.match"},{"type":"sss.settle"}]`.
- Group keys and compute operands are top-level fields only (no `Customer/Country` yet).
- Aggregate methods: `sum`, `average`, `min`, `max`, and `$count`. Compute ops: `add`, `sub`, `mul`, `div`.
- `$count` counts instances, not a property: `aggregate($count as Total)` and `groupby((country,status),aggregate($count as Count))`. Every input row counts, including a row that omits a property or holds a non-numeric value. An empty whole-set aggregate is `[{"Total":0}]`. A groupby of an empty set is `[]`. `amount with $count` is not this form.
- Compute/aggregate aliases cannot be `_id` (**422**). `filter(...)` / `compute(...)` / `aggregate(...)` / `groupby(...)` require a balanced closing `)` with no trailing junk. `aggregate($count)` without `as <alias>` is **422**.

```bash
curl -s "http://127.0.0.1:2112/tasks?\$apply=groupby((type))"
curl -s "http://127.0.0.1:2112/orders?\$apply=groupby((status),aggregate(amount%20with%20sum%20as%20Total))"
curl -s "http://127.0.0.1:2112/orders?\$apply=groupby((country,status),aggregate(amount%20with%20sum%20as%20Total))"
curl -s "http://127.0.0.1:2112/orders?\$apply=groupby((country,status))"
curl -s "http://127.0.0.1:2112/orders?\$apply=aggregate(\$count%20as%20Total)"
curl -s "http://127.0.0.1:2112/orders?\$apply=groupby((status),aggregate(\$count%20as%20Count))"
curl -s "http://127.0.0.1:2112/orders?\$apply=filter(status%20eq%20'active')/groupby((country),aggregate(amount%20with%20sum%20as%20Total))"
curl -s "http://127.0.0.1:2112/lines?\$compute=Price%20mul%20Qty%20as%20LineTotal&\$select=LineTotal"
```

Tests: `./tools/CB.sh debug test "groupby" --jsonl=failures` and `./tools/CB.sh debug test "compute" --jsonl=failures`.
