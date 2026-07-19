# OData support and roadmap

YarDB implements a practical **subset** of OData 4.x for document collections over HTTP. This page is the checklist of what works today vs what is planned. Runtime details and examples for shipped options live in [programs.md](programs.md#odata-query-parameters).

## Supported today

### Query options

| Option | Notes |
|--------|--------|
| `$top` / `$skip` | Client-driven paging |
| `$orderby` | Single field, `asc` / `desc` |
| `$filter` | See below |
| `$select` | Projection; `_id` always included |
| `$count=true` | Returns a JSON number; works with `$filter` |
| `$expand` | v1: `{singular}_id` → plural collection `_id` |
| `$compute` | `Price mul Qty as LineTotal` (also `add`/`sub`/`div`); top-level numeric fields; runs after expand, before `$filter` / `$select` / paging (aliases usable in `$filter` / `$orderby`) |
| `$apply` | Pipeline of slash-separated transforms: `filter(...)`, `compute(...)`, `groupby((field),aggregate(...))`, `aggregate(...)`; methods `sum`/`average`/`min`/`max` |

### `$filter`

| Feature | Notes |
|---------|--------|
| Comparisons | `eq`, `ne`, `gt`, `ge`, `lt`, `le` |
| Logic | `not`, `and`, `or` (precedence: `not` > `and` > `or`); `not` is `!match(P)` (not `$ne`/`$lte` rewrites); nested/double `not` restores positive comparison branches; expressions that expand to more than 64 OR branches, nest more than 64 `not` operators, or exceed 8192 characters are rejected |
| `in` | Strings or numbers |
| Nested paths | `Customer/Country eq 'USA'` |
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
| Multi-field `$orderby` | `$orderby=country,age desc` |
| `$search` | `$search=wireless` |
| `$skiptoken` / `@odata.nextLink` | Server-driven paging |
| Collection lambdas | `Lines/any(l:l/Qty gt 0)` |
| Richer string/date functions | `indexof`, `tolower`, `year(...)` |
| `$batch` | `POST /$batch` |
| Delta queries | `$deltaToken` |
| Bound actions / functions | `POST /users(1)/Default.ResetPassword` |
| XML CSDL `$metadata` | Beyond JSON CSDL |

## Design notes for `$apply`

- Slash-separated transforms outside quotes/parens: `filter(...)/compute(...)/groupby(...)`.
- Sibling query `$filter` still pre-filters the read; a pipeline `filter(...)` narrows further.
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
