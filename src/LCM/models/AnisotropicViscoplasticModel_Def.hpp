//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include <Intrepid2_MiniTensor.h>
#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

#include "LocalNonlinearSolver.hpp"

namespace LCM
{

//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
AnisotropicViscoplasticModel<EvalT, Traits>::
AnisotropicViscoplasticModel(Teuchos::ParameterList* p,
    const Teuchos::RCP<Albany::Layouts>& dl) :
    LCM::ConstitutiveModel<EvalT, Traits>(p, dl)
{
  // retrive appropriate field name strings
  std::string cauchy_string = (*field_name_map_)["Cauchy_Stress"];
  std::string Fp_string = (*field_name_map_)["Fp"];
  std::string eqps_string = (*field_name_map_)["eqps"];
  std::string ess_string = (*field_name_map_)["ess"];
  std::string kappa_string = (*field_name_map_)["iso_Hardening"];
  std::string source_string = (*field_name_map_)["Mechanical_Source"];
  std::string F_string = (*field_name_map_)["F"];
  std::string J_string = (*field_name_map_)["J"];

  // define the dependent fields
  this->dep_field_map_.insert(std::make_pair(F_string, dl->qp_tensor));
  this->dep_field_map_.insert(std::make_pair(J_string, dl->qp_scalar));
  this->dep_field_map_.insert(std::make_pair("Poissons Ratio", dl->qp_scalar));
  this->dep_field_map_.insert(std::make_pair("Elastic Modulus", dl->qp_scalar));
  this->dep_field_map_.insert(std::make_pair("Yield Strength", dl->qp_scalar));
  this->dep_field_map_.insert(
      std::make_pair("Flow Rule Coefficient", dl->qp_scalar));
  this->dep_field_map_.insert(
      std::make_pair("Flow Rule Exponent", dl->qp_scalar));
  this->dep_field_map_.insert(std::make_pair("Yield Strength", dl->qp_scalar));
  this->dep_field_map_.insert(
      std::make_pair("Hardening Modulus", dl->qp_scalar));
  this->dep_field_map_.insert(
      std::make_pair("Recovery Modulus", dl->qp_scalar));
  this->dep_field_map_.insert(std::make_pair("Delta Time", dl->workset_scalar));

  // define the evaluated fields
  this->eval_field_map_.insert(std::make_pair(cauchy_string, dl->qp_tensor));
  this->eval_field_map_.insert(std::make_pair(Fp_string, dl->qp_tensor));
  this->eval_field_map_.insert(std::make_pair(eqps_string, dl->qp_scalar));
  if (have_temperature_) {
    this->eval_field_map_.insert(std::make_pair(source_string, dl->qp_scalar));
  }

  // define the state variables
  //
  // stress
  this->num_state_variables_++;
  this->state_var_names_.push_back(cauchy_string);
  this->state_var_layouts_.push_back(dl->qp_tensor);
  this->state_var_init_types_.push_back("scalar");
  this->state_var_init_values_.push_back(0.0);
  this->state_var_old_state_flags_.push_back(false);
  this->state_var_output_flags_.push_back(p->get<bool>("Output Cauchy Stress", false));
  //
  // Fp
  this->num_state_variables_++;
  this->state_var_names_.push_back(Fp_string);
  this->state_var_layouts_.push_back(dl->qp_tensor);
  this->state_var_init_types_.push_back("identity");
  this->state_var_init_values_.push_back(0.0);
  this->state_var_old_state_flags_.push_back(true);
  this->state_var_output_flags_.push_back(p->get<bool>("Output Fp", false));
  //
  // eqps
  this->num_state_variables_++;
  this->state_var_names_.push_back(eqps_string);
  this->state_var_layouts_.push_back(dl->qp_scalar);
  this->state_var_init_types_.push_back("scalar");
  this->state_var_init_values_.push_back(0.0);
  this->state_var_old_state_flags_.push_back(true);
  this->state_var_output_flags_.push_back(p->get<bool>("Output eqps", false));
  //
  // ess
  this->num_state_variables_++;
  this->state_var_names_.push_back(ess_string);
  this->state_var_layouts_.push_back(dl->qp_scalar);
  this->state_var_init_types_.push_back("scalar");
  this->state_var_init_values_.push_back(0.0);
  this->state_var_old_state_flags_.push_back(true);
  this->state_var_output_flags_.push_back(p->get<bool>("Output ess", false));
  //
  // kappa - isotropic hardening
  this->num_state_variables_++;
  this->state_var_names_.push_back(kappa_string);
  this->state_var_layouts_.push_back(dl->qp_scalar);
  this->state_var_init_types_.push_back("scalar");
  this->state_var_init_values_.push_back(0.0);
  this->state_var_old_state_flags_.push_back(true);
  this->state_var_output_flags_.push_back(p->get<bool>("Output kappa", false));
  //
  // mechanical source
  if (have_temperature_) {
    this->num_state_variables_++;
    this->state_var_names_.push_back(source_string);
    this->state_var_layouts_.push_back(dl->qp_scalar);
    this->state_var_init_types_.push_back("scalar");
    this->state_var_init_values_.push_back(0.0);
    this->state_var_old_state_flags_.push_back(false);
    this->state_var_output_flags_.push_back(p->get<bool>("Output Mechanical Source", false));
  }
}
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
void AnisotropicViscoplasticModel<EvalT, Traits>::
computeState(typename Traits::EvalData workset,
    std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT>>> dep_fields,
    std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT>>> eval_fields)
{
  std::string cauchy_string = (*field_name_map_)["Cauchy_Stress"];
  std::string Fp_string = (*field_name_map_)["Fp"];
  std::string eqps_string = (*field_name_map_)["eqps"];
  std::string ess_string = (*field_name_map_)["ess"];
  std::string kappa_string = (*field_name_map_)["iso_Hardening"];
  std::string source_string = (*field_name_map_)["Mechanical_Source"];
  std::string F_string = (*field_name_map_)["F"];
  std::string J_string = (*field_name_map_)["J"];

  // extract dependent MDFields
  PHX::MDField<ScalarT> def_grad = *dep_fields[F_string];
  PHX::MDField<ScalarT> J = *dep_fields[J_string];
  PHX::MDField<ScalarT> poissons_ratio = *dep_fields["Poissons Ratio"];
  PHX::MDField<ScalarT> elastic_modulus = *dep_fields["Elastic Modulus"];
  PHX::MDField<ScalarT> yield_strength = *dep_fields["Yield Strength"];
  PHX::MDField<ScalarT> hardening_modulus = *dep_fields["Hardening Modulus"];
  PHX::MDField<ScalarT> recovery_modulus = *dep_fields["Recovery Modulus"];
  PHX::MDField<ScalarT> flow_exp = *dep_fields["Flow Rule Exponent"];
  PHX::MDField<ScalarT> flow_coeff = *dep_fields["Flow Rule Coefficient"];
  PHX::MDField<ScalarT> delta_time = *dep_fields["Delta Time"];

  // extract evaluated MDFields
  PHX::MDField<ScalarT> stress = *eval_fields[cauchy_string];
  PHX::MDField<ScalarT> Fp = *eval_fields[Fp_string];
  PHX::MDField<ScalarT> eqps = *eval_fields[eqps_string];
  PHX::MDField<ScalarT> ess = *eval_fields[ess_string];
  PHX::MDField<ScalarT> kappa = *eval_fields[kappa_string];
  PHX::MDField<ScalarT> source;
  if (have_temperature_) {
    source = *eval_fields[source_string];
  }

  // get State Variables
  Albany::MDArray Fpold = (*workset.stateArrayPtr)[Fp_string + "_old"];
  Albany::MDArray eqpsold = (*workset.stateArrayPtr)[eqps_string + "_old"];

  ScalarT bulk, mu, mubar, K, Y;
  ScalarT Jm23, trace, smag2, smag, f, p, dgam;
  ScalarT sq23(std::sqrt(2. / 3.));

  Intrepid2::Tensor<ScalarT> F(num_dims_), be(num_dims_), s(num_dims_), sigma(
      num_dims_);
  Intrepid2::Tensor<ScalarT> N(num_dims_), A(num_dims_), expA(num_dims_), Fpnew(
      num_dims_);
  Intrepid2::Tensor<ScalarT> I(Intrepid2::eye<ScalarT>(num_dims_));
  Intrepid2::Tensor<ScalarT> Fpn(num_dims_), Cpinv(num_dims_), Fe(num_dims_);
  Intrepid2::Tensor<ScalarT> tau(num_dims_), M(num_dims_);

  for (int cell(0); cell < workset.numCells; ++cell) {
    for (int pt(0); pt < num_pts_; ++pt) {
      bulk = elastic_modulus(cell, pt)
          / (3. * (1. - 2. * poissons_ratio(cell, pt)));
      mu = elastic_modulus(cell, pt) / (2. * (1. + poissons_ratio(cell, pt)));
      K = hardening_modulus(cell, pt);
      Y = yield_strength(cell, pt);
      Jm23 = std::pow(J(cell, pt), -2. / 3.);

      // fill local tensors
      F.fill(def_grad, cell, pt,0,0);
      //Fpn.fill( &Fpold(cell,pt,int(0),int(0)) );
      for (int i(0); i < num_dims_; ++i) {
        for (int j(0); j < num_dims_; ++j) {
          Fpn(i, j) = ScalarT(Fpold(cell, pt, i, j));
        }
      }

      // compute trial state
      // compute the Kirchhoff stress in the current configuration
      //
      Fe = F * Intrepid2::inverse(Fpn);
      Cpinv = Intrepid2::inverse(Fpn) * Intrepid2::transpose(Intrepid2::inverse(Fpn));
      be = F * Cpinv * Intrepid2::transpose(F);
      ScalarT Je = std::sqrt( Intrepid2::det(be));
      s = mu * Intrepid2::dev(be);
      p = 0.5 * bulk * (Je * Je - 1.);
      tau = p * I + s;
      
      // pull back the Kirchhoff stress to the intermediate configuration
      // this is the Mandel stress
      //
      M = Intrepid2::transpose(Fe) * tau * Intrepid2::inverse(Intrepid2::transpose(Fe));
      
      // check yield condition
      smag = Intrepid2::norm(s);
      f = smag - sq23 * (Y + K * eqpsold(cell, pt));

      if (f > 1E-12) {
        // return mapping algorithm
        bool converged = false;
        ScalarT g = f;
        ScalarT H = 0.0;
        ScalarT dH = 0.0;
        ScalarT alpha = 0.0;
        ScalarT res = 0.0;
        int count = 0;
        dgam = 0.0;

        LocalNonlinearSolver<EvalT, Traits> solver;

        std::vector<ScalarT> F(1);
        std::vector<ScalarT> dFdX(1);
        std::vector<ScalarT> X(1);

        F[0] = f;
        X[0] = 0.0;
        dFdX[0] = (-2. * mubar) * (1. + H / (3. * mubar));
        while (!converged && count <= 30)
        {
          count++;
          solver.solve(dFdX, X, F);
          alpha = eqpsold(cell, pt) + sq23 * X[0];
          H = K * alpha;
          dH = K;
          F[0] = smag - (2. * mubar * X[0] + sq23 * (Y + H));
          dFdX[0] = -2. * mubar * (1. + dH / (3. * mubar));

          res = std::abs(F[0]);
          if (res < 1.e-11 || res / f < 1.E-11)
            converged = true;

          TEUCHOS_TEST_FOR_EXCEPTION(count == 30, std::runtime_error,
              std::endl <<
              "Error in return mapping, count = " <<
              count <<
              "\nres = " << res <<
              "\nrelres = " << res/f <<
              "\ng = " << F[0] <<
              "\ndg = " << dFdX[0] <<
              "\nalpha = " << alpha << std::endl);
        }
        solver.computeFadInfo(dFdX, X, F);
        dgam = X[0];

        // plastic direction
        N = (1 / smag) * s;

        // update s
        s -= 2 * mubar * dgam * N;

        // update eqps
        eqps(cell, pt) = alpha;

        // mechanical source
        if (have_temperature_ && delta_time(0) > 0) {
          source(cell, pt) = (sq23 * dgam / delta_time(0)
            * (Y + H + temperature_(cell,pt))) / (density_ * heat_capacity_);
        }

        // exponential map to get Fpnew
        A = dgam * N;
        expA = Intrepid2::exp(A);
        Fpnew = expA * Fpn;
        for (int i(0); i < num_dims_; ++i) {
          for (int j(0); j < num_dims_; ++j) {
            Fp(cell, pt, i, j) = Fpnew(i, j);
          }
        }
      } else {
        eqps(cell, pt) = eqpsold(cell, pt);
        if (have_temperature_) source(cell, pt) = 0.0;
        for (int i(0); i < num_dims_; ++i) {
          for (int j(0); j < num_dims_; ++j) {
            Fp(cell, pt, i, j) = Fpn(i, j);
          }
        }
      }

      // compute pressure
      p = 0.5 * bulk * (J(cell, pt) - 1. / (J(cell, pt)));

      // compute stress
      sigma = p * I + s / J(cell, pt);
      for (int i(0); i < num_dims_; ++i) {
        for (int j(0); j < num_dims_; ++j) {
          stress(cell, pt, i, j) = sigma(i, j);
        }
      }
    }
  }

  if (have_temperature_) {
    for (int cell(0); cell < workset.numCells; ++cell) {
      for (int pt(0); pt < num_pts_; ++pt) {
        F.fill(def_grad,cell,pt,0,0);
        ScalarT J = Intrepid2::det(F);
        sigma.fill(stress,cell,pt,0,0);
        sigma -= 3.0 * expansion_coeff_ * (1.0 + 1.0 / (J*J))
          * (temperature_(cell,pt) - ref_temperature_) * I;
        for (int i = 0; i < num_dims_; ++i) {
          for (int j = 0; j < num_dims_; ++j) {
            stress(cell, pt, i, j) = sigma(i, j);
          }
        }
      }
    }
  }

}
//------------------------------------------------------------------------------
}

