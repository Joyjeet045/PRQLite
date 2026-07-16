#include "vm/column_store.hpp"

#include "vm/table_manager.hpp"

namespace db::vm {

void ColumnStore::invalidate(int tableId) { cache_.erase(tableId); }

const TableColumns& ColumnStore::getOrBuild(int tableId, const Schema& schema,
                                            TableManager& tables) {
    auto it = cache_.find(tableId);
    if (it != cache_.end()) return it->second;

    TableColumns tc;
    tc.columns.resize(schema.size());
    for (std::size_t c = 0; c < schema.size(); ++c) tc.columns[c].type = schema[c];

    for (TableIterator iter(&tables, tableId); iter.valid(); iter.next()) {
        Tuple tuple = Tuple::deserialize(iter.bytes(), schema);
        for (std::size_t c = 0; c < schema.size(); ++c) {
            Column& col = tc.columns[c];
            if (c >= tuple.size() || tuple.at(static_cast<int>(c)).isNull()) {
                col.isNull.push_back(1);
                col.ints.push_back(0);
                col.doubles.push_back(0.0);
                col.bools.push_back(0);
                col.texts.emplace_back();
                continue;
            }
            const Value& v = tuple.at(static_cast<int>(c));
            col.isNull.push_back(0);
            col.ints.push_back(v.type == ValueType::Int ? v.intValue : 0);
            col.doubles.push_back(v.type == ValueType::Double ? v.doubleValue : 0.0);
            col.bools.push_back(v.type == ValueType::Bool ? (v.boolValue ? 1 : 0) : 0);
            col.texts.push_back(v.type == ValueType::Text ? v.textValue : std::string());
        }
        ++tc.rows;
    }

    auto [ins, ok] = cache_.emplace(tableId, std::move(tc));
    (void)ok;
    return ins->second;
}

namespace {

Value columnValue(const Column& col, std::size_t row) {
    if (col.isNull[row]) return Value::null();
    switch (col.type) {
        case parser::DataType::Int:
            return Value::makeInt(col.ints[row]);
        case parser::DataType::Float:
            return Value::makeDouble(col.doubles[row]);
        case parser::DataType::Bool:
            return Value::makeBool(col.bools[row] != 0);
        default:
            return Value::makeText(col.texts[row]);
    }
}

bool termPasses(const VecPredicate::Term& term, const Column& col, std::size_t row) {
    if (col.isNull[row]) return false;
    Value v = columnValue(col, row);
    auto cmp = compareValues(v, term.literal);
    if (!cmp.has_value()) return false;
    switch (term.op) {
        case parser::ComparisonOp::Eq: return *cmp == 0;
        case parser::ComparisonOp::Neq: return *cmp != 0;
        case parser::ComparisonOp::Lt: return *cmp < 0;
        case parser::ComparisonOp::Leq: return *cmp <= 0;
        case parser::ComparisonOp::Gt: return *cmp > 0;
        case parser::ComparisonOp::Geq: return *cmp >= 0;
    }
    return false;
}

}  // namespace

std::vector<Value> columnarAggregate(const TableColumns& table,
                                     const std::vector<VecAggregate>& aggregates,
                                     const std::optional<VecPredicate>& predicate) {
    std::vector<Value> out;
    out.reserve(aggregates.size());

    for (const VecAggregate& agg : aggregates) {
        std::int64_t count = 0;
        std::int64_t isum = 0;
        double dsum = 0.0;
        bool isFloat = false;
        bool hasBest = false;
        Value best;

        const Column* col =
            agg.column >= 0 && agg.column < static_cast<int>(table.columns.size())
                ? &table.columns[agg.column]
                : nullptr;

        for (std::size_t r = 0; r < table.rows; ++r) {
            if (predicate) {
                bool ok = true;
                for (const VecPredicate::Term& term : predicate->terms) {
                    if (term.column < 0 ||
                        term.column >= static_cast<int>(table.columns.size()) ||
                        !termPasses(term, table.columns[term.column], r)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;
            }

            if (agg.kind == VecAggregate::Kind::CountStar) {
                ++count;
                continue;
            }
            if (col == nullptr || col->isNull[r]) continue;

            switch (agg.kind) {
                case VecAggregate::Kind::Count:
                    ++count;
                    break;
                case VecAggregate::Kind::Sum:
                case VecAggregate::Kind::Avg:
                    if (col->type == parser::DataType::Float) {
                        isFloat = true;
                        dsum += col->doubles[r];
                    } else {
                        isum += col->ints[r];
                    }
                    ++count;
                    break;
                case VecAggregate::Kind::Min:
                case VecAggregate::Kind::Max: {
                    Value v = columnValue(*col, r);
                    if (!hasBest) {
                        best = v;
                        hasBest = true;
                    } else {
                        auto cmp = compareValues(v, best);
                        if (cmp.has_value()) {
                            if (agg.kind == VecAggregate::Kind::Min && *cmp < 0) best = v;
                            if (agg.kind == VecAggregate::Kind::Max && *cmp > 0) best = v;
                        }
                    }
                    break;
                }
                case VecAggregate::Kind::CountStar:
                    break;
            }
        }

        switch (agg.kind) {
            case VecAggregate::Kind::CountStar:
            case VecAggregate::Kind::Count:
                out.push_back(Value::makeInt(count));
                break;
            case VecAggregate::Kind::Sum:
                if (count == 0) out.push_back(Value::null());
                else if (isFloat) out.push_back(Value::makeDouble(dsum + static_cast<double>(isum)));
                else out.push_back(Value::makeInt(isum));
                break;
            case VecAggregate::Kind::Avg:
                if (count == 0) {
                    out.push_back(Value::null());
                } else {
                    double total = dsum + static_cast<double>(isum);
                    out.push_back(Value::makeDouble(total / static_cast<double>(count)));
                }
                break;
            case VecAggregate::Kind::Min:
            case VecAggregate::Kind::Max:
                out.push_back(hasBest ? best : Value::null());
                break;
        }
    }
    return out;
}

}  // namespace db::vm
