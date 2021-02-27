/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                       */
/*    This file is part of the HiGHS linear optimization suite           */
/*                                                                       */
/*    Written and engineered 2008-2020 at the University of Edinburgh    */
/*                                                                       */
/*    Available as open-source under the MIT License                     */
/*                                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/**@file HighsPostsolveStack.h
 * @brief Class to hold all information for postsolve and can transform back
 * primal and dual solutions.
 * @author Leona Gottwald
 */

#ifndef PRESOLVE_HIGHS_POSTSOLVE_STACK_H_
#define PRESOLVE_HIGHS_POSTSOLVE_STACK_H_

#include <cassert>
#include <cmath>
#include <numeric>
#include <tuple>
#include <vector>

#include "lp_data/HConst.h"
#include "lp_data/HStruct.h"
#include "util/HighsCDouble.h"
#include "util/HighsDataStack.h"
#include "util/HighsMatrixSlice.h"

namespace presolve {
class HighsPostsolveStack {
  // now a section of individual classes for each type of each transformation
  // step that requires postsolve starts each class gets as first argument the
  // current stack of ReductionValues and custom arguments that contain the
  // necessary information that is required to undo the transformation. The
  // constructor is responsible for storing all necessary information in class
  // members and the reduction value stack. The class members should be as slim
  // as possible and putting values on the reduction value stack should be
  // preferred, because the classes are stored in a discriminated union and the
  // largest size counts. The classes should implement an undo() function which
  // gets the ReductionValues as argument and can be expected to be called such
  // that the stack is in the state as after the constructor has been called.
  // The undo() call must pop all values from the stack that were added during
  // the constructor call, and should restore primal/dual solution values, as
  // well as the basis status as appropriate.
 public:
  enum class RowType {
    Geq,
    Leq,
    Eq,
  };

 private:
  struct FreeColSubstitution {
    double rhs;
    double colCost;
    int row;
    int col;
    RowType rowType;

    void undo(const std::vector<std::pair<int, double>>& rowValues,
              const std::vector<std::pair<int, double>>& colValues,
              HighsSolution& solution, HighsBasis& basis);
  };

  struct DoubletonEquation {
    double coef;
    double coefSubst;
    double rhs;
    double substLower;
    double substUpper;
    double substCost;
    int row;
    int colSubst;
    int col;
    bool lowerTightened;
    bool upperTightened;

    void undo(const std::vector<std::pair<int, double>>& colValues,
              HighsSolution& solution, HighsBasis& basis);
  };

  struct EqualityRowAddition {
    int row;
    int addedEqRow;
    double eqRowScale;

    void undo(HighsSolution& solution, HighsBasis& basis);
  };

  struct ForcingColumn {
    double colCost;
    int col;
    bool atInfiniteUpper;

    void undo(const std::vector<std::pair<int, double>>& colValues,
              HighsSolution& solution, HighsBasis& basis);
  };

  struct SingletonRow {
    double coef;
    int row;
    int col;
    bool colLowerTightened;
    bool colUpperTightened;

    void undo(HighsSolution& solution, HighsBasis& basis);
  };

  // column fixed to lower or upper bound
  struct FixedCol {
    double fixValue;
    double colCost;
    int col;
    HighsBasisStatus fixType;

    void undo(const std::vector<std::pair<int, double>>& colValues,
              HighsSolution& solution, HighsBasis& basis);
  };

  struct RedundantRow {
    int row;

    void undo(HighsSolution& solution, HighsBasis& basis);
  };

  struct ForcingRow {
    double side;
    int row;
    RowType rowType;

    void undo(const std::vector<std::pair<int, double>>& rowValues,
              HighsSolution& solution, HighsBasis& basis);
  };

  struct DuplicateRow {
    double duplicateRowScale;
    int duplicateRow;
    int row;
    bool rowLowerTightened;
    bool rowUpperTightened;

    void undo(HighsSolution& solution, HighsBasis& basis);
  };

  struct DuplicateColumn {
    double colScale;
    double colLower;
    double colUpper;
    double duplicateColLower;
    double duplicateColUpper;
    int col;
    int duplicateCol;
    bool colIntegral;
    bool duplicateColIntegral;

    void undo(HighsSolution& solution, HighsBasis& basis, double feastol);
  };

  /// tags for reduction
  enum class ReductionType : uint8_t {
    kFreeColSubstitution,
    kDoubletonEquation,
    kEqualityRowAddition,
    kSingletonRow,
    kFixedCol,
    kRedundantRow,
    kForcingRow,
    kDuplicateRow,
    kDuplicateColumn,
  };

  HighsDataStack reductionValues;
  std::vector<ReductionType> reductions;
  std::vector<int> origColIndex;
  std::vector<int> origRowIndex;
  std::vector<std::pair<int, double>> rowValues;
  std::vector<std::pair<int, double>> colValues;
  int origNumCol = -1;
  int origNumRow = -1;

 public:
  int getOrigRowIndex(int row) const { return origRowIndex[row]; }

  int getOrigColIndex(int col) const { return origColIndex[col]; }

  void initializeIndexMaps(int numRow, int numCol);

  void compressIndexMaps(const std::vector<int>& newRowIndex,
                         const std::vector<int>& newColIndex);

  template <typename RowStorageFormat, typename ColStorageFormat>
  void freeColSubstitution(int row, int col, double rhs, double colCost,
                           RowType rowType,
                           const HighsMatrixSlice<RowStorageFormat>& rowVec,
                           const HighsMatrixSlice<ColStorageFormat>& colVec) {
    rowValues.clear();
    for (const HighsSliceNonzero& rowVal : rowVec)
      rowValues.emplace_back(origColIndex[rowVal.index()], rowVal.value());

    colValues.clear();
    for (const HighsSliceNonzero& colVal : colVec)
      colValues.emplace_back(origRowIndex[colVal.index()], colVal.value());

    reductionValues.push(FreeColSubstitution{rhs, colCost, origRowIndex[row],
                                             origColIndex[col], rowType});
    reductionValues.push(rowValues);
    reductionValues.push(colValues);
    reductions.push_back(ReductionType::kFreeColSubstitution);
  }

  template <typename ColStorageFormat>
  void doubletonEquation(int row, int colSubst, int col, double coefSubst,
                         double coef, double rhs, double substLower,
                         double substUpper, double oldLower, double oldUpper,
                         double newLower, double newUpper, double substCost,
                         const HighsMatrixSlice<ColStorageFormat>& colVec) {
    colValues.clear();
    for (const HighsSliceNonzero& colVal : colVec)
      colValues.emplace_back(origRowIndex[colVal.index()], colVal.value());

    reductionValues.push(DoubletonEquation{
        coef, coefSubst, rhs, substLower, substUpper, substCost,
        origRowIndex[row], origColIndex[colSubst], origColIndex[col],
        (oldLower < newLower), (oldUpper > newUpper)});
    reductionValues.push(colValues);
    reductions.push_back(ReductionType::kDoubletonEquation);
  }

  void equalityRowAddition(int row, int addedEqRow, double eqRowScale) {
    reductionValues.push(EqualityRowAddition{
        origRowIndex[row], origRowIndex[addedEqRow], eqRowScale});
    reductions.push_back(ReductionType::kEqualityRowAddition);
  }

  void singletonRow(int row, int col, double coef, bool tightenedColLower,
                    bool tightenedColUpper) {
    reductionValues.push(SingletonRow{coef, origRowIndex[row],
                                      origColIndex[col], tightenedColLower,
                                      tightenedColUpper});
    reductions.push_back(ReductionType::kSingletonRow);
  }

  template <typename ColStorageFormat>
  void fixedColAtLower(int col, double fixValue, double colCost,
                       const HighsMatrixSlice<ColStorageFormat>& colVec) {
    assert(std::isfinite(fixValue));
    colValues.clear();
    for (const HighsSliceNonzero& colVal : colVec)
      colValues.emplace_back(origRowIndex[colVal.index()], colVal.value());

    reductionValues.push(FixedCol{fixValue, colCost, origColIndex[col],
                                  HighsBasisStatus::LOWER});
    reductionValues.push(colValues);
    reductions.push_back(ReductionType::kFixedCol);
  }

  template <typename ColStorageFormat>
  void fixedColAtUpper(int col, double fixValue, double colCost,
                       const HighsMatrixSlice<ColStorageFormat>& colVec) {
    assert(std::isfinite(fixValue));
    colValues.clear();
    for (const HighsSliceNonzero& colVal : colVec)
      colValues.emplace_back(origRowIndex[colVal.index()], colVal.value());

    reductionValues.push(FixedCol{fixValue, colCost, origColIndex[col],
                                  HighsBasisStatus::UPPER});
    reductionValues.push(colValues);
    reductions.push_back(ReductionType::kFixedCol);
  }

  template <typename ColStorageFormat>
  void removedFixedCol(int col, double fixValue, double colCost,
                       const HighsMatrixSlice<ColStorageFormat>& colVec) {
    assert(std::isfinite(fixValue));
    colValues.clear();
    for (const HighsSliceNonzero& colVal : colVec)
      colValues.emplace_back(origRowIndex[colVal.index()], colVal.value());

    reductionValues.push(FixedCol{fixValue, colCost, origColIndex[col],
                                  HighsBasisStatus::NONBASIC});
    reductionValues.push(colValues);
    reductions.push_back(ReductionType::kFixedCol);
  }

  void redundantRow(int row) {
    reductionValues.push(RedundantRow{row});
    reductions.push_back(ReductionType::kRedundantRow);
  }

  template <typename RowStorageFormat>
  void forcingRow(int row, const HighsMatrixSlice<RowStorageFormat>& rowVec,
                  double side, RowType rowType) {
    rowValues.clear();
    for (const HighsSliceNonzero& rowVal : rowVec)
      rowValues.emplace_back(origColIndex[rowVal.index()], rowVal.value());

    reductionValues.push(ForcingRow{side, origRowIndex[row], rowType});
    reductionValues.push(rowValues);
    reductions.push_back(ReductionType::kForcingRow);
  }

  void duplicateRow(int row, bool rowUpperTightened, bool rowLowerTightened,
                    int duplicateRow, double duplicateRowScale) {
    reductionValues.push(
        DuplicateRow{duplicateRowScale, origRowIndex[duplicateRow],
                     origRowIndex[row], rowLowerTightened, rowUpperTightened});
    reductions.push_back(ReductionType::kDuplicateRow);
  }

  void duplicateColumn(double colScale, double colLower, double colUpper,
                       double duplicateColLower, double duplicateColUpper,
                       int col, int duplicateCol, bool colIntegral,
                       bool duplicateColIntegral) {
    reductionValues.push(DuplicateColumn{
        colScale, colLower, colUpper, duplicateColLower, duplicateColUpper,
        origColIndex[col], origColIndex[duplicateCol], colIntegral,
        duplicateColIntegral});
    reductions.push_back(ReductionType::kDuplicateColumn);
  }

  void undo(HighsSolution& solution, HighsBasis& basis, double feastol) {
    reductionValues.resetPosition();

    if (solution.col_value.size() != origColIndex.size()) return;
    if (solution.row_value.size() != origRowIndex.size()) return;

    bool dualPostSolve = solution.col_dual.size() == solution.col_value.size();

    // expand solution to original index space
    solution.col_value.resize(origNumCol);
    for (int i = origColIndex.size() - 1; i >= 0; --i) {
      assert(origColIndex[i] >= i);
      solution.col_value[origColIndex[i]] = solution.col_value[i];
    }

    solution.row_value.resize(origNumRow);
    for (int i = origRowIndex.size() - 1; i >= 0; --i) {
      assert(origRowIndex[i] >= i);
      solution.row_value[origRowIndex[i]] = solution.row_value[i];
    }

    if (dualPostSolve) {
      // if dual solution is given, expand dual solution and basis to original
      // index space
      solution.col_dual.resize(origNumCol);
      basis.col_status.resize(origNumCol);
      for (int i = origColIndex.size() - 1; i >= 0; --i) {
        basis.col_status[origColIndex[i]] = basis.col_status[i];
        solution.col_dual[origColIndex[i]] = solution.col_dual[i];
      }

      solution.row_dual.resize(origNumRow);
      basis.row_status.resize(origNumRow);
      for (int i = origRowIndex.size() - 1; i >= 0; --i) {
        basis.row_status[origRowIndex[i]] = basis.row_status[i];
        solution.row_dual[origRowIndex[i]] = solution.row_dual[i];
      }
    }

    // now undo the changes
    for (int i = reductions.size() - 1; i >= 0; --i) {
      switch (reductions[i]) {
        case ReductionType::kFreeColSubstitution: {
          FreeColSubstitution reduction;
          reductionValues.pop(colValues);
          reductionValues.pop(rowValues);
          reductionValues.pop(reduction);
          reduction.undo(rowValues, colValues, solution, basis);
          break;
        }
        case ReductionType::kDoubletonEquation: {
          DoubletonEquation reduction;
          reductionValues.pop(colValues);
          reductionValues.pop(reduction);
          reduction.undo(colValues, solution, basis);
          break;
        }
        case ReductionType::kEqualityRowAddition: {
          EqualityRowAddition reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis);
          break;
        }
        case ReductionType::kSingletonRow: {
          SingletonRow reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis);
          break;
        }
        case ReductionType::kFixedCol: {
          FixedCol reduction;
          reductionValues.pop(colValues);
          reductionValues.pop(reduction);
          reduction.undo(colValues, solution, basis);
          break;
        }
        case ReductionType::kRedundantRow: {
          RedundantRow reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis);
          break;
        }
        case ReductionType::kForcingRow: {
          ForcingRow reduction;
          reductionValues.pop(rowValues);
          reductionValues.pop(reduction);
          reduction.undo(rowValues, solution, basis);
          break;
        }
        case ReductionType::kDuplicateRow: {
          DuplicateRow reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis);
          break;
        }
        case ReductionType::kDuplicateColumn: {
          DuplicateColumn reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis, feastol);
        }
      }
    }
  }

  void undoUntil(HighsSolution& solution, HighsBasis& basis, double feastol,
                 int numReductions) {
    reductionValues.resetPosition();

    if (solution.col_value.size() != origColIndex.size()) return;
    if (solution.row_value.size() != origRowIndex.size()) return;

    bool dualPostSolve = solution.col_dual.size() == solution.col_value.size();

    // expand solution to original index space
    solution.col_value.resize(origNumCol);
    for (int i = origColIndex.size() - 1; i >= 0; --i) {
      assert(origColIndex[i] >= i);
      solution.col_value[origColIndex[i]] = solution.col_value[i];
    }

    solution.row_value.resize(origNumRow);
    for (int i = origRowIndex.size() - 1; i >= 0; --i) {
      assert(origRowIndex[i] >= i);
      solution.row_value[origRowIndex[i]] = solution.row_value[i];
    }

    if (dualPostSolve) {
      // if dual solution is given, expand dual solution and basis to original
      // index space
      solution.col_dual.resize(origNumCol);
      basis.col_status.resize(origNumCol);
      for (int i = origColIndex.size() - 1; i >= 0; --i) {
        basis.col_status[origColIndex[i]] = basis.col_status[i];
        solution.col_dual[origColIndex[i]] = solution.col_dual[i];
      }

      solution.row_dual.resize(origNumRow);
      basis.row_status.resize(origNumRow);
      for (int i = origRowIndex.size() - 1; i >= 0; --i) {
        basis.row_status[origRowIndex[i]] = basis.row_status[i];
        solution.row_dual[origRowIndex[i]] = solution.row_dual[i];
      }
    }

    // now undo the changes
    for (int i = reductions.size() - 1; i >= numReductions; --i) {
      switch (reductions[i]) {
        case ReductionType::kFreeColSubstitution: {
          FreeColSubstitution reduction;
          reductionValues.pop(colValues);
          reductionValues.pop(rowValues);
          reductionValues.pop(reduction);
          reduction.undo(rowValues, colValues, solution, basis);
          break;
        }
        case ReductionType::kDoubletonEquation: {
          DoubletonEquation reduction;
          reductionValues.pop(colValues);
          reductionValues.pop(reduction);
          reduction.undo(colValues, solution, basis);
          break;
        }
        case ReductionType::kEqualityRowAddition: {
          EqualityRowAddition reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis);
          break;
        }
        case ReductionType::kSingletonRow: {
          SingletonRow reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis);
          break;
        }
        case ReductionType::kFixedCol: {
          FixedCol reduction;
          reductionValues.pop(colValues);
          reductionValues.pop(reduction);
          reduction.undo(colValues, solution, basis);
          break;
        }
        case ReductionType::kRedundantRow: {
          RedundantRow reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis);
          break;
        }
        case ReductionType::kForcingRow: {
          ForcingRow reduction;
          reductionValues.pop(rowValues);
          reductionValues.pop(reduction);
          reduction.undo(rowValues, solution, basis);
          break;
        }
        case ReductionType::kDuplicateRow: {
          DuplicateRow reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis);
          break;
        }
        case ReductionType::kDuplicateColumn: {
          DuplicateColumn reduction;
          reductionValues.pop(reduction);
          reduction.undo(solution, basis, feastol);
        }
      }
    }
  }

  size_t numReductions() const { return reductions.size(); }
};

}  // namespace presolve

#endif