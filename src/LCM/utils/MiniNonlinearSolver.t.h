//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

namespace LCM
{

//
// miniMinimizer
//
template<typename MIN, typename STEP, typename FN, Intrepid2::Index N>
void
miniMinimize(
    MIN & minimizer,
    STEP & step_method,
    FN & function,
    Intrepid2::Vector<PHAL::AlbanyTraits::Residual::ScalarT, N> & soln)
{
  minimizer.solve(step_method, function, soln);

  return;
}

template<typename MIN, typename STEP, typename FN, typename T, Intrepid2::Index N>
void
miniMinimize(
    MIN & minimizer,
    STEP & step_method,
    FN & function,
    Intrepid2::Vector<T, N> & soln)
{
// Make sure that if Albany is compiled with a static FAD type
// there won't be confusion with MiniSolver's FAD.
  using AD = Intrepid2::FAD<RealType, N>;

  static_assert(
      std::is_same<T, AD>::value == false,
      "Albany and MiniSolver Fad types not allowed to be equal.");

  using ValueT = typename Sacado::ValueType<T>::type;

  Intrepid2::Vector<ValueT, N>
  soln_val = Sacado::Value<Intrepid2::Vector<T, N>>::eval(soln);

  minimizer.solve(step_method, function, soln_val);

  auto const
  dimension = soln.get_dimension();

  // Put values back in solution vector
  for (auto i = 0; i < dimension; ++i) {
    soln(i).val() = soln_val(i);
  }

  // Get the Hessian evaluated at the solution.
  Intrepid2::Tensor<ValueT, N>
  DrDx = function.hessian(soln_val);

  // Now compute gradient with solution that has Albany sensitivities.
  Intrepid2::Vector<T, N>
  resi = function.gradient(soln);

  // Solve for solution sensitivities.
  computeFADInfo(resi, DrDx, soln);

  return;
}

//
//
//
template<typename T, typename S, Intrepid2::Index N>
void
computeFADInfo(
    Intrepid2::Vector<T, N> const & r,
    Intrepid2::Tensor<S, N> const & DrDx,
    Intrepid2::Vector<T, N> & x)
{
  // Check whether dealing with AD type.
  if (Sacado::IsADType<T>::value == false) return;

  //Deal with derivative information
  auto const
  dimension = r.get_dimension();

  assert(dimension > 0);

  auto const
  order = r[0].size();

  assert(order > 0 && "FATAL ERROR: Expected Fad info but there is none!");

  // Extract sensitivities of r wrt p
  Intrepid2::Matrix<S, N>
  DrDp(dimension, order);

  for (auto i = 0; i < dimension; ++i) {
    for (auto j = 0; j < order; ++j) {
      DrDp(i, j) = r(i).dx(j);
    }
  }

  // Solve for all DxDp
  Intrepid2::Matrix<S, N>
  DxDp = Intrepid2::solve(DrDx, DrDp);

  // Pack into x.
  for (auto i = 0; i < dimension; ++i) {
    x(i).resize(order);
    for (auto j = 0; j < order; ++j) {
      x(i).fastAccessDx(j) = -DxDp(i, j);
    }
  }
}

#ifdef ALBANY_ENSEMBLE
#endif // ALBANY_ENSEMBLE

} // namespace LCM
