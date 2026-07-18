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
| `$apply` | v1: `groupby((field),aggregate(...))` or `aggregate(...)`; methods `sum`/`average`/`min`/`max`; optional `$filter` first |

### `$filter`

| Feature | Notes |
|---------|--------|
| Comparisons | `eq`, `ne`, `gt`, `ge`, `lt`, `le` |
| Logic | `not`, `and`, `or` (precedence: `not` > `and` > `or`) |
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
| Deeper `$expand` | `$expand=customer($select=name)`, nested expand, multi-valued refs | Planned |
| Richer `$apply` | Multi-key groupby, `filter(...)/groupby(...)`, `$count` aggregate | Planned |

### Later

| Feature | Example |
|---------|---------|
| `$apply` filter/transform chain | `$apply=filter(...)/groupby(...)` |
| Multi-field `$orderby` | `$orderby=country,age desc` |
| `$search` | `$search=wireless` |
| `$compute` | `$compute=Price mul Qty as LineTotal` |
| `$skiptoken` / `@odata.nextLink` | Server-driven paging |
| Collection lambdas | `Lines/any(l:l/Qty gt 0)` |
| Richer string/date functions | `indexof`, `tolower`, `year(...)` |
| `$batch` | `POST /$batch` |
| Delta queries | `$deltaToken` |
| Bound actions / functions | `POST /users(1)/Default.ResetPassword` |
| XML CSDL `$metadata` | Beyond JSON CSDL |

## Design notes for `$apply` v1

- Optional `$filter` may run **before** aggregation (read matching docs, then group).
- v1 does **not** compose with `$expand`, `$select`, `$orderby`, `$top`, `$skip`, or `$count` (rejected with **422**).
- Response is a JSON **array** of group objects, e.g. `[{"status":"active","Total":42},…]`.
- Group keys are top-level fields only in v1 (no `Customer/Country` grouping yet).
- Aggregate methods: `sum`, `average`, `min`, `max`.

```bash
curl -s "http://127.0.0.1:2112/orders?\$apply=groupby((status),aggregate(amount%20with%20sum%20as%20Total))"
```

Tests: `./tools/CB.sh debug test "groupby" --jsonl=failures`.
