#pragma once
#include <boost/rational.hpp>
#include <cereal/access.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/variant.hpp>
#include <cereal/types/vector.hpp>
#include <cmath>
#include <foresighter/common/hash.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include "foresighter/common/cartesian_product.hpp"
#include "foresighter/common/exceptions.hpp"
#include "foresighter/common/hashset.hpp"
#include "foresighter/common/variant.hpp"

namespace {
[[nodiscard]] inline bool hasDoubleRepresentation(const std::string& str,
                                                  double& d) noexcept {
  char* pEnd = nullptr;
  d = std::strtod(str.data(), &pEnd);
  return pEnd == str.data() + str.size();
}

// Optimized string normalization with single pass
[[nodiscard]] inline std::string normalizeString(const std::string& str) {
  std::string result;
  result.reserve(str.length() * 2);  // Pre-allocate worst case
  for (char c : str) {
    if (c == '\'') {
      result.append("''");
    } else {
      result += c;
    }
  }
  return result;
}

}  // namespace

namespace foresighter::implementation::sql {

class ConcreteTable {
 public:
  class ColumnDataType {
   public:
    enum class Type { NUMERIC, TEXT };

    explicit ColumnDataType(const std::string& s) : type(parseType(s)) {}

    explicit ColumnDataType(int sqlite3DataType)
        : type(parseSqliteType(sqlite3DataType)) {}

    explicit ColumnDataType(Type type) : type(type) {}

    [[nodiscard]] bool operator==(const Type t) const noexcept {
      return type == t;
    }

    [[nodiscard]] std::string toString() const noexcept {
      return type == Type::NUMERIC ? "NUMERIC" : "TEXT";
    }

    [[nodiscard]] int getType() const noexcept {
      return type == Type::NUMERIC ? 0 : 1;
    }

    [[nodiscard]] bool operator==(const ColumnDataType& other) const noexcept {
      return type == other.type;
    }

    [[nodiscard]] size_t getHash() const noexcept {
      return static_cast<size_t>(type);
    }

    [[nodiscard]] bool operator<(const ColumnDataType& other) const noexcept {
      // NUMERIC < TEXT
      return type == Type::NUMERIC && other.type == Type::TEXT;
    }

   private:
    Type type;

    static Type parseType(const std::string& s) {
      if (s == "Str" || s == "str" || s == "datetime" || s == "Date" ||
          s == "bool") {
        return Type::TEXT;
      }
      if (s == "Int" || s == "int" || s == "real" || s == "Dbl" || s == "dbl") {
        return Type::NUMERIC;
      }
      throw common::InvalidArgument("Unexpected column data type: " +
                                    std::string(s));
    }

    static Type parseSqliteType(int type) {
      if (type == 1 || type == 2)
        return Type::NUMERIC;
      if (type == 3 || type == 4)
        return Type::TEXT;
      throw common::InvalidArgument(
          "Unexpected column data type NULL from sqlite3");
    }
  };

  class FixedPointValue {
   public:
    constexpr static long precision = 1000000;

    explicit FixedPointValue(int n) noexcept : rational(n) {}

    explicit FixedPointValue(long n) noexcept : rational(n) {}

    explicit FixedPointValue(double n) {
      const double integral = std::floor(n);
      const double fractional = n - integral;
      const long fractionalPart = std::lround(fractional * precision);
      const long gcd_ = boost::gcd(fractionalPart, precision);
      const long denominator = precision / gcd_;
      const long numerator = fractionalPart / gcd_;
      rational = boost::rational<long>(
          numerator + static_cast<long>(integral * denominator), denominator);
    }

    [[nodiscard]] long getNumerator() const noexcept {
      return rational.numerator();
    }

    [[nodiscard]] long getDenominator() const noexcept {
      return rational.denominator();
    }

    [[nodiscard]] bool operator==(const FixedPointValue& other) const noexcept {
      const double fdiff = std::abs(toDouble() - other.toDouble());
      return fdiff < 11.0 / precision;
    }

    [[nodiscard]] bool operator<(const FixedPointValue& other) const noexcept {
      return *this != other && rational < other.rational;
    }

    [[nodiscard]] double toDouble() const noexcept {
      return static_cast<double>(rational.numerator()) / rational.denominator();
    }

    [[nodiscard]] std::string toString() const {
      const double val = toDouble();
      if (std::floor(val) == std::ceil(val)) {
        return std::to_string(static_cast<long>(val));
      }
      return std::to_string(val);
    }

    [[nodiscard]] size_t getHash() const noexcept {
      //NOTE: the hash here have to insure that if value 1 and value 2 is within 11/precision, then their hash is the same.
      //this is because the hash is used to compare the values, and if the values are the same, then the hash should be the same.
      //this is to avoid the case where the values are the same, but the hash is different, which will cause the inconsistent mappings to be missed.

      const double val = toDouble();
      // Round to nearest multiple of 11/precision to ensure values within that range hash the same
      const long roundedVal =
          std::round(val * precision / 11.0) * 11.0 / precision;
      // Use rounded value to generate hash
      return std::hash<long>{}(roundedVal);
    }

   private:
    boost::rational<long> rational;
    friend class cereal::access;

    template <typename Archive>
    void save(Archive& archive) const {
      archive(rational.numerator(), rational.denominator());
    }

    template <typename Archive>
    void load(Archive& archive) {
      long numerator, denominator;
      archive(numerator, denominator);
      rational = boost::rational<long>(numerator, denominator);
    }
  };

  class NullValue {
   public:
    NullValue() = default;

    bool operator==(const NullValue&) const noexcept { return true; }

    [[nodiscard]] size_t getHash() const noexcept { return 1; }
  };

  struct CellValue {
    explicit CellValue(int n) noexcept : value(FixedPointValue(n)) {}

    explicit CellValue(long n) noexcept : value(FixedPointValue(n)) {}

    explicit CellValue(double n) : value(FixedPointValue(n)) {}

    void InitWithString(const std::string& s, bool isNum) {
      if (s == "NULL") {
        value = NullValue();
        return;
      }
      if (isNum) {
        double d;
        if (hasDoubleRepresentation(s, d)) {
          value = FixedPointValue(d);
        } else {
          throw common::InvalidArgument("Cell value " + std::string(s) +
                                        " is not a number");
        }
      } else {
        value = std::string(s);
      }
    }

    explicit CellValue(const std::string& s, bool isNum = false) {
      InitWithString(s, isNum);
    }

    explicit CellValue(const char* s, bool isNum = false) {
      if (!s) {
        value = NullValue();
      } else {
        InitWithString(s, isNum);
      }
    }

    [[nodiscard]] bool operator==(const CellValue& other) const noexcept {
      return value == other.value;
    }

    [[nodiscard]] bool operator<(const CellValue& other) const {
      if (value == other.value)
        return false;
      return toString() < other.toString();
    }

    [[nodiscard]] bool operator==(int n) const noexcept {
      if (const auto* fp = std::get_if<FixedPointValue>(&value)) {
        return fp->getNumerator() % fp->getDenominator() == 0 &&
               fp->getNumerator() / fp->getDenominator() == n;
      }
      return false;
    }

    [[nodiscard]] std::string toString() const {
      if (const auto* str = std::get_if<std::string>(&value)) {
        return "'" + normalizeString(*str) + "'";
      }
      if (const auto* fp = std::get_if<FixedPointValue>(&value)) {
        return fp->toString();
      }
      return "NULL";
    }

    [[nodiscard]] size_t getHash() const noexcept {
      return std::visit(
          foresighter::common::overloaded{
              [](const std::string& s) { return std::hash<std::string>()(s); },
              [](const FixedPointValue& fp) { return fp.getHash(); },
              [](const NullValue& n) {
                return n.getHash();
              }},
          value);
    }

    std::variant<std::string, FixedPointValue, NullValue> value;
    friend class cereal::access;

    template <typename Archive>
    void serialize(Archive& archive) {
      archive(value);
    }
  };

  using Row = std::vector<CellValue>;
  using Table = std::vector<Row>;

  ConcreteTable() = delete;

  explicit ConcreteTable(Table table,
                         std::vector<ColumnDataType> column_data_types) {
    sortTable(table);
    _table = std::move(table);
    _column_data_types = std::move(column_data_types);
    handleWithAllNullColTypes();
    updateHash();
  }

  [[nodiscard]] const Table& getTable() const noexcept { return _table; }

  [[nodiscard]] const Row& getRow(size_t rowIdx) const {
    return _table[rowIdx];
  }

  [[nodiscard]] const std::vector<ColumnDataType>& getColumnDataTypes()
      const noexcept {
    return _column_data_types;
  }

  [[nodiscard]] size_t getNumRows() const noexcept { return _table.size(); }

  [[nodiscard]] size_t getNumColumns() const noexcept {
    return _table.empty() ? getColumnDataTypes().size() : _table[0].size();
  }

  [[nodiscard]] bool operator==(const ConcreteTable& other) const noexcept {
    return _table == other._table;
  }

  void handleWithAllNullColTypes() {
    _all_null_types.resize(getNumColumns(), false);
    //if for a column, all the values are null value, then we set the column data type to text
    for (int i = 0; i < getNumColumns(); ++i) {
      if (std::all_of(_table.begin(), _table.end(), [i](const Row& row) {
            return std::holds_alternative<NullValue>(row[i].value);
          })) {
        _all_null_types[i] = true;
      }
    }
  }

  [[nodiscard]] std::vector<std::vector<int>>
  getConsistentMappingsUnderProjection(const ConcreteTable& other) const {
    //see if the current table is consistent with "other" table under projection
    //that is, if for every column in "other" table, it can find a column in the current table such that
    //they are the same. If so, add the mapping to the result.

    if (other.getNumRows() != getNumRows()) {
      return {};
    }

    auto getPlausibleMappings = [this,
                                 &other]() -> std::vector<std::vector<int>> {
      //plauible mappings are mappings that the column data matches under multiset semantics. Note that when combining multiple columns into tuples, they may not match.
      //this step is to narrow down the mappings to the plausible ones. Otherwise checking all the mappings is too expensive.
      std::vector<std::vector<int>> mappings(other.getNumColumns());
      //get all columns hashes
      std::vector<size_t> otherColumnHashes;
      std::vector<size_t> thisColumnHashes;

      for (int i = 0; i < other.getNumColumns(); ++i) {
        size_t h = 0;
        h += other.getColumnDataTypes()[i].getHash();
        for (int j = 0; j < other.getNumRows(); ++j) {
          h += other.getRow(j)[i].getHash();
        }
        otherColumnHashes.push_back(h);
      }

      for (int i = 0; i < getNumColumns(); ++i) {
        size_t h = 0;
        h += getColumnDataTypes()[i].getHash();
        for (int j = 0; j < getNumRows(); ++j) {
          h += getRow(j)[i].getHash();
        }
        thisColumnHashes.push_back(h);
      }

      for (int i = 0; i < other.getNumColumns(); ++i) {
        for (int j = 0; j < getNumColumns(); ++j) {
          if (otherColumnHashes[i] == thisColumnHashes[j]) {
            mappings[i].push_back(j);
          }
        }
        if (mappings[i].empty()) {
          return {};
        }
      }

      //return the cartesian product of all the mappings
      foresighter::common::CartesianProductIterator iter(mappings);
      std::vector<std::vector<int>> result;
      result.reserve(iter.size());
      while (iter.hasNext()) {
        result.push_back(iter.next());
      }
      return result;
    };

    auto getProjectedTable = [](const ConcreteTable& table,
                                const std::vector<int>& mapping)
        -> std::vector<std::vector<CellValue>> {
      std::vector<std::vector<CellValue>> result;
      result.reserve(table.getNumRows());
      for (int i = 0; i < table.getNumRows(); ++i) {
        std::vector<CellValue> projectedRow;
        for (int j = 0; j < mapping.size(); ++j) {
          projectedRow.push_back(table.getRow(i)[mapping[j]]);
        }
        result.push_back(projectedRow);
      }
      //sort the projected table, by the first column, then the second column, etc.
      ConcreteTable::sortTable(result);
      return result;
    };

    auto getConsistentMappings =
        [this, getProjectedTable](
            const std::vector<std::vector<int>>& mappings,
            const ConcreteTable& other) -> std::vector<std::vector<int>> {
      std::vector<std::vector<int>> result;
      for (const auto& mapping : mappings) {
        auto projectedTable = getProjectedTable(*this, mapping);
        if (projectedTable == other.getTable()) {
          result.push_back(mapping);
        }
      }
      return result;
    };

    auto plausibleMappings = getPlausibleMappings();
    if (plausibleMappings.empty()) {
      return {};
    }

    auto consistentMappings = getConsistentMappings(plausibleMappings, other);

    return consistentMappings;
  }

  template <typename T>
  [[nodiscard]] bool implies(const T& other) const noexcept {
    if (std::holds_alternative<ConcreteTable>(other)) {
      return _table == std::get<ConcreteTable>(other)._table;
    }
    return false;
  }

  [[nodiscard]] std::string toString() const {
    if (_table.empty())
      return {};

    const size_t rowSize = _table[0].size();
    std::string result;
    result.reserve(_table.size() * (rowSize * 10));

    for (const auto& row : _table) {
      for (const auto& cell : row) {
        result.append(cell.toString());
        result += ' ';
      }
      result += '\n';
    }
    return result;
  }

  [[nodiscard]] size_t getHash() const noexcept { return _cachedHash; }

 private:
  Table _table;
  std::vector<ColumnDataType> _column_data_types;
  size_t _cachedHash{0};
  std::vector<bool> _all_null_types{};

  void updateHash() noexcept {
    size_t h = 0;
    for (const auto& row : _table) {
      for (const auto& cell : row) {
        foresighter::common::hashCombine(h, cell.value);
      }
    }
    for (int i = 0; i < getNumColumns(); ++i) {
      if (_all_null_types[i]) {
        foresighter::common::hashCombine(h, 12345);
      } else {
        foresighter::common::hashCombine(h, _column_data_types[i].getHash());
      }
    }
    _cachedHash = h;
  }

  static void sortTable(Table& table) {
    std::sort(table.begin(), table.end(), [](const Row& a, const Row& b) {
      const size_t size = a.size();
      for (size_t i = 0; i < size; ++i) {
        if (a[i] < b[i])
          return true;
        if (!(a[i] == b[i]))
          return false;
      }
      return false;
    });
  }

  friend class cereal::access;

  template <typename Archive>
  void serialize(Archive& archive) {
    archive(_table);
    archive(_column_data_types);
    archive(_all_null_types);
    updateHash();
  }
};

class InputTable {
 public:
  InputTable() = delete;

  explicit InputTable(
      ConcreteTable::Table table,
      std::vector<ConcreteTable::ColumnDataType> column_data_types)
      : _table(std::move(table), std::move(column_data_types)) {
    std::vector<std::pair<ConcreteTable::ColumnDataType, int>>
        columnTypeWithColumnIndex;
    for (int i = 0; i < _table.getColumnDataTypes().size(); ++i) {
      columnTypeWithColumnIndex.push_back(
          std::make_pair(_table.getColumnDataTypes()[i], i));
    }
    std::sort(columnTypeWithColumnIndex.begin(),
              columnTypeWithColumnIndex.end(),
              [](const std::pair<ConcreteTable::ColumnDataType, int>& a,
                 const std::pair<ConcreteTable::ColumnDataType, int>& b) {
                return a.first < b.first;
              });
    for (int i = 0; i < columnTypeWithColumnIndex.size(); ++i) {
      _columnCanonicalOrderMapping.push_back(
          columnTypeWithColumnIndex[i].second);
    }
    //reorder the concrete table following the canonical order
    ConcreteTable::Table reorderedTable;
    //reordered table has the same dimension as the original table, but we reorder the columns following _columnCanonicalOrderMapping
    for (int i = 0; i < _table.getTable().size(); ++i) {
      std::vector<ConcreteTable::CellValue> reorderedRow;
      for (int j = 0; j < _columnCanonicalOrderMapping.size(); ++j) {
        reorderedRow.push_back(
            _table.getTable()[i][_columnCanonicalOrderMapping[j]]);
      }
      reorderedTable.push_back(reorderedRow);
    }
    std::vector<ConcreteTable::ColumnDataType> reorderedColumnDataTypes;
    for (int i = 0; i < _columnCanonicalOrderMapping.size(); ++i) {
      reorderedColumnDataTypes.push_back(
          _table.getColumnDataTypes()[_columnCanonicalOrderMapping[i]]);
    }
    _table = std::move(ConcreteTable(reorderedTable, reorderedColumnDataTypes));
  }

  [[nodiscard]] const ConcreteTable& getConcreteTable() const noexcept {
    return _table;
  }

  [[nodiscard]] const std::vector<int>& getColumnCanonicalOrderMapping()
      const noexcept {
    return _columnCanonicalOrderMapping;
  }

 private:
  ConcreteTable _table;
  std::vector<int> _columnCanonicalOrderMapping;
};

}  // namespace foresighter::implementation::sql

namespace std {
template <>
struct hash<foresighter::implementation::sql::ConcreteTable::FixedPointValue> {
  [[nodiscard]] size_t operator()(
      const foresighter::implementation::sql::ConcreteTable::FixedPointValue&
          value) const noexcept {
    return value.getHash();
  }
};

template <>
struct hash<foresighter::implementation::sql::ConcreteTable::NullValue> {
  [[nodiscard]] size_t operator()(
      const foresighter::implementation::sql::ConcreteTable::NullValue& value)
      const noexcept {
    return value.getHash();
  }
};

template <>
struct hash<foresighter::implementation::sql::ConcreteTable::CellValue> {
  [[nodiscard]] size_t operator()(
      const foresighter::implementation::sql::ConcreteTable::CellValue& cell)
      const {
    return cell.getHash();
  }
};

template <>
struct hash<foresighter::implementation::sql::ConcreteTable> {
  [[nodiscard]] size_t operator()(
      const foresighter::implementation::sql::ConcreteTable& table)
      const noexcept {
    return table.getHash();
  }
};

template <>
struct hash<foresighter::implementation::sql::InputTable> {
  [[nodiscard]] size_t operator()(
      const foresighter::implementation::sql::InputTable& table)
      const noexcept {
    return table.getConcreteTable().getHash();
  }
};
}  // namespace std
