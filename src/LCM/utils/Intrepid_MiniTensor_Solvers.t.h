//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

namespace Intrepid2
{

//
//
//
template<typename Function_Derived, typename S>
template<typename T, Index N>
T
Function_Base<Function_Derived, S>::
value(Function_Derived & f, Vector<T, N> const & x)
{
  Index const
  dimension = x.get_dimension();

  assert(dimension == Function_Derived::DIMENSION);

  Vector<T, N> const
  r = f.gradient(x);

  return 0.5 * dot(r, r);
}

//
//
//
template<typename Function_Derived, typename S>
template<typename T, Index N>
Vector<T, N>
Function_Base<Function_Derived, S>::
gradient(Function_Derived & f, Vector<T, N> const & x)
{
  using AD = FAD<T, N>;

  Index const
  dimension = x.get_dimension();

  assert(dimension == Function_Derived::DIMENSION);

  Vector<AD, N>
  x_ad(dimension);

  for (Index i{0}; i < dimension; ++i) {
    x_ad(i) = AD(dimension, i, x(i));
  }

  AD const
  f_ad = f.value(x_ad);

  Vector<T, N>
  gradient(dimension);

  for (Index i{0}; i < dimension; ++i) {
    gradient(i) = f_ad.dx(i);
  }

  return gradient;
}

//
//
//
template<typename Function_Derived, typename S>
template<typename T, Index N>
Tensor<T, N>
Function_Base<Function_Derived, S>::
hessian(Function_Derived & f, Vector<T, N> const & x)
{
  using AD = FAD<T, N>;

  Index const
  dimension = x.get_dimension();

  assert(dimension == Function_Derived::DIMENSION);

  Vector<AD, N>
  x_ad(dimension);

  for (Index i{0}; i < dimension; ++i) {
    x_ad(i) = AD(dimension, i, x(i));
  }

  Vector<AD, N> const
  r_ad = f.gradient(x_ad);

  Tensor<T, N>
  Hessian(dimension);

  for (Index i{0}; i < dimension; ++i) {
    for (Index j{0}; j < dimension; ++j) {
      Hessian(i, j) = r_ad(i).dx(j);
    }
  }

  return Hessian;
}

//
//
//
template<typename T, Index N>
template<typename STEP, typename FN>
void
Minimizer<T, N>::
solve(STEP & step_method, FN & fn, Vector<T, N> & soln)
{
  step_method_name = STEP::NAME;
  function_name = FN::NAME;
  initial_guess = soln;

  Vector<T, N>
  resi = fn.gradient(soln);

  initial_norm = norm(resi);

  updateConvergenceCriterion(initial_norm);

  step_method.initialize(fn, soln, resi);

  while (continueSolve() == true) {

    Vector<T, N> const
    step = step_method.step(fn, soln, resi);

    soln += step;

    resi = fn.gradient(soln);

    T const
    norm_resi = norm(resi);

    updateConvergenceCriterion(norm_resi);
    ++num_iter;
  }

  recordFinals(fn, soln);
  return;
}

//
//
//
template<typename T, Index N>
void
Minimizer<T, N>::
updateConvergenceCriterion(T const ae)
{
  abs_error = ae;
  rel_error = initial_norm > 0.0 ? abs_error / initial_norm : T(0.0);

  bool const
  converged_absolute = abs_error <= abs_tol;

  bool const
  converged_relative = rel_error <= rel_tol;

  converged = converged_absolute || converged_relative;

  return;
}

//
//
//
template<typename T, Index N>
bool
Minimizer<T, N>::
continueSolve() const
{
  bool const
  is_max_iter = num_iter >= max_num_iter;

  bool const
  continue_solve = is_max_iter == false && converged == false;

  return continue_solve;
}

//
//
//
template<typename T, Index N>
void
Minimizer<T, N>::
printReport(std::ostream & os)
{
  char const * const
  converged_string = converged == true ? "YES" : "NO";

  // Happy / frowny face
  //char const * const
  //converged_string = converged == true ? "\U0001F60A" : "\U0001F623";

  os << "\n\n";
  os << "Method     : " << step_method_name << '\n';
  os << "Function   : " << function_name << '\n';
  os << "Converged  : " << converged_string << '\n';
  os << "Max Iters  : " << max_num_iter << '\n';
  os << "Iters Taken: " << num_iter << '\n';

  os << std::scientific << std::setprecision(16);

  os << "Initial |R|: " << std::setw(24) << initial_norm << '\n';
  os << "Abs Tol    : " << std::setw(24) << abs_tol << '\n';
  os << "Abs Error  : " << std::setw(24) << abs_error << '\n';
  os << "Rel Tol    : " << std::setw(24) << rel_tol << '\n';
  os << "Rel Error  : " << std::setw(24) << rel_error << '\n';
  os << "Initial X  : " << initial_guess << '\n';
  os << "Final X    : " << final_soln << '\n';
  os << "f(X)       : " << std::setw(24) << final_value << '\n';
  os << "Df(X)      : " << final_gradient << '\n';
  os << "DDf(X)     : " << final_hessian << '\n';
  os << '\n';

  return;
}

//
// Trust region subproblem. Exact algorithm, Nocedal 2nd Ed 4.3
//
template<typename T, Index N>
Vector<T, N>
TrustRegionExact<T, N>::
step(Tensor<T, N> const & Hessian, Vector<T, N> const & gradient)
{
  Index const
  dimension = gradient.get_dimension();

  Tensor<T, N> const
  I = identity<T, N>(dimension);

  Vector<T, N>
  step(dimension);

  T
  lambda = initial_lambda;

  for (Index i{0}; i < max_num_iter; ++i) {

    Tensor<T, N> const
    K = Hessian + lambda * I;

    Tensor<T, N> const
    L = cholesky(K).first;

    step = - Intrepid2::solve(K, gradient);

    Vector<T, N> const
    q = Intrepid2::solve(L, step);

    T const
    np = norm(step);

    T const
    nps = np * np;

    T const
    nqs = norm_square(q);

    T const
    lambda_incr = nps * (np - region_size) / nqs / region_size;

    lambda += std::max(lambda_incr, 0.0);

  }

  return step;
}

//
// Newton line search
//
template<typename T, Index N>
template<typename FN>
Vector<T, N>
NewtonLineSearch<T, N>::
step(FN & fn, Vector<T, N> const & direction, Vector<T, N> const & soln)
{
  Index const
  dimension = soln.get_dimension();

  Vector<T, N>
  step(dimension, ZEROS);

  T const
  projection_direction = dot(direction, direction);

  for (Index i{0}; i < max_num_iter; ++i) {

    Vector<T, N> const
    soln_next = soln + step;

    Vector<T, N> const
    gradient_next = fn.gradient(soln_next);

    Tensor<T, N> const
    Hessian_next = fn.hessian(soln_next);

    T const
    projection = dot(gradient_next, direction);

    T const
    contraction = dot(direction, dot(Hessian_next, direction));

    T const
    step_length = - projection / contraction;

    step += step_length * direction;

    T const
    ls_length2 = step_length * step_length * projection_direction;

    bool const
    line_search_converged = ls_length2 <= tolerance * tolerance;

    if (line_search_converged == true) break;

  }

  return step;
}

//
//
//
template<typename T, Index N>
template<typename FN>
void
NewtonStep<T, N>::
initialize(FN &, Vector<T, N> const &, Vector<T, N> const &)
{
  return;
}

//
// Plain Newton step.
//
template<typename T, Index N>
template<typename FN>
Vector<T, N>
NewtonStep<T, N>::
step(FN & fn, Vector<T, N> const & soln, Vector<T, N> const & resi)
{
  Tensor<T, N> const
  Hessian = fn.hessian(soln);

  Vector<T, N> const
  step = - Intrepid2::solve(Hessian, resi);

  return step;
}

//
//
//
template<typename T, Index N>
template<typename FN>
void
TrustRegionStep<T, N>::
initialize(FN &, Vector<T, N> const &, Vector<T, N> const &)
{
  region_size = initial_region_size;

  return;
}

//
// Trust Region method.  See Nocedal's algorithm 11.5.
//
template<typename T, Index N>
template<typename FN>
Vector<T, N>
TrustRegionStep<T, N>::
step(FN & fn, Vector<T, N> const & soln, Vector<T, N> const & resi)
{
  Tensor<T, N> const
  Hessian = fn.hessian(soln);

  // Trust region subproblem. Exact algorithm, Nocedal 2nd Ed 4.3
  TrustRegionExact<T, N>
  tr_exact;

  tr_exact.initial_lambda = 0.0;
  tr_exact.region_size = region_size;

  Vector<T, N>
  step = tr_exact.step(Hessian, resi);

  Vector<T, N> const
  soln_next = soln + step;

  Vector<T, N> const
  resi_next = fn.gradient(soln_next);

  // Compute reduction factor \rho_k in Nocedal's algorithm 11.5
  T const
  nr = norm_square(resi);

  T const
  nrp = norm_square(resi_next);

  T const
  nrKp = norm_square(resi + dot(Hessian, step));

  T const
  reduction = (nr - nrp) / (nr - nrKp);

  // Determine whether the trust region should be increased, decreased
  // or left the same.
  T const
  computed_size = norm(step);

  if (reduction < 0.25) {

    region_size = 0.25 * computed_size;

  } else {

    bool const
    at_boundary = std::abs(computed_size / region_size - 1.0) <= 1.0e-8;

    bool const
    increase_region_size = reduction > 0.75 && at_boundary;

    if (increase_region_size == true) {
      region_size = std::min(2.0 * region_size, max_region_size);
    }

  }

  if (reduction <= min_reduction) {
    step.fill(ZEROS);
  }

  return step;
}

//
//
//
template<typename T, Index N>
template<typename FN>
void
ConjugateGradientStep<T, N>::
initialize(FN & fn, Vector<T, N> const & soln, Vector<T, N> const & gradient)
{
  Tensor<T, N> const
  Hessian = fn.hessian(soln);

  precon_resi = - Intrepid2::solve(Hessian, gradient);

  search_direction = precon_resi;

  projection_new = - dot(gradient, search_direction);

  restart_directions_counter = 0;

  return;
}

//
// Conjugate Gradient Method step.
// For now the Gram-Schmidt method is fixed to Polak-Ribiere
// and preconditioning with the Hessian.
// This is taken from J.R. Shewchuck "painless" conjugate gradient
// manuscript that is all over the place on the net.
//
template<typename T, Index N>
template<typename FN>
Vector<T, N>
ConjugateGradientStep<T, N>::
step(FN & fn, Vector<T, N> const & soln, Vector<T, N> const &)
{
  Index const
  dimension = soln.get_dimension();

  // Newton line search.
  NewtonLineSearch<T, N>
  newton_ls;

  Vector<T, N> const
  step = newton_ls.step(fn, search_direction, soln);

  Vector<T, N> const
  soln_next = soln + step;

  Vector<T, N> const
  gradient_next = fn.gradient(soln_next);

  T const
  projection_old = projection_new;

  T const
  projection_mid = - dot(gradient_next, precon_resi);

  Tensor<T, N> const
  Hessian = fn.hessian(soln_next);

  precon_resi = - Intrepid2::solve(Hessian, gradient_next);

  projection_new = - dot(gradient_next, precon_resi);

  T const
  gram_schmidt_factor = (projection_new - projection_mid) / projection_old;

  ++restart_directions_counter;

  bool const
  rewind = restart_directions_counter == restart_directions_interval;

  bool const
  bad_directions = gram_schmidt_factor <= 0.0;

  bool const
  restart_directions = rewind || bad_directions;

  if (restart_directions == true) {

    search_direction = precon_resi;
    restart_directions_counter = 0;

  } else {

    search_direction = precon_resi + gram_schmidt_factor * search_direction;

  }

  return step;
}

//
//
//
template<typename T, Index N>
template<typename FN>
void
LineSearchRegularizedStep<T, N>::
initialize(FN &, Vector<T, N> const &, Vector<T, N> const &)
{
  return;
}

//
// Line Search Newton-like method.  See Nocedal's algorithm 11.4.
//
template<typename T, Index N>
template<typename FN>
Vector<T, N>
LineSearchRegularizedStep<T, N>::
step(FN & fn, Vector<T, N> const & soln, Vector<T, N> const & gradient)
{
  Index const
  dimension = soln.get_dimension();

  Tensor<T, N> const
  Hessian = fn.hessian(soln);

  Vector<T, N>
  step(dimension);

  bool const
  singular_hessian = std::abs(det(Hessian)) < hessian_singular_tol;

  bool const
#ifdef ALBANY_USE_PUBLICTRILINOS
  ill_conditioned_hessian = cond(Hessian) > hessian_cond_tol;
#else
  ill_conditioned_hessian = inv_cond(Hessian) * hessian_cond_tol < 1.0;
#endif

  bool const
  bad_hessian = singular_hessian || ill_conditioned_hessian;

  // Regularize Hessian if it is bad.
  if (bad_hessian == true) {

    // Trust region subproblem. Exact algorithm, Nocedal 2nd Ed 4.3
    TrustRegionExact<T, N>
    tr_exact;

    tr_exact.initial_lambda = 1.0;
    tr_exact.region_size = step_length;

    step = tr_exact.step(Hessian, gradient);

  } else {

    // Standard Newton step
    step = - Intrepid2::solve(Hessian, gradient);

  }

  // Newton line search.
  NewtonLineSearch<T, N>
  newton_ls;

  Vector<T, N> const
  ls_step = newton_ls.step(fn, step, soln);

  return ls_step;
}

} // namespace Intrepid2
