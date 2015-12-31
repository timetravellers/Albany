//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include <Intrepid2_MiniTensor.h>
#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

namespace LCM
{

//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
HyperelasticDamageModel<EvalT, Traits>::
HyperelasticDamageModel(Teuchos::ParameterList* p,
    const Teuchos::RCP<Albany::Layouts>& dl) :
    LCM::ConstitutiveModel<EvalT, Traits>(p, dl),
      max_damage_(p->get<RealType>("Maximum Damage", 1.0)),
      damage_saturation_(p->get<RealType>("Damage Saturation", 1.0))
{
  // define the dependent fields
  this->dep_field_map_.insert(std::make_pair("F", dl->qp_tensor));
  this->dep_field_map_.insert(std::make_pair("J", dl->qp_scalar));
  this->dep_field_map_.insert(std::make_pair("Poissons Ratio", dl->qp_scalar));
  this->dep_field_map_.insert(std::make_pair("Elastic Modulus", dl->qp_scalar));
  this->dep_field_map_.insert(std::make_pair("Delta Time", dl->workset_scalar));

  // define the evaluated fields
  std::string cauchy = (*field_name_map_)["Cauchy_Stress"];
  this->eval_field_map_.insert(std::make_pair(cauchy, dl->qp_tensor));
  this->eval_field_map_.insert(std::make_pair("Damage_Source", dl->qp_scalar));
  this->eval_field_map_.insert(std::make_pair("alpha", dl->qp_scalar));

  // deal with damage
  if (have_damage_) {
    this->dep_field_map_.insert(std::make_pair("damage", dl->qp_scalar));
  } else {
    this->eval_field_map_.insert(std::make_pair("local damage", dl->qp_scalar));
  }

  // define the state variables
  //
  this->num_state_variables_++;
  this->state_var_names_.push_back(cauchy);
  this->state_var_layouts_.push_back(dl->qp_tensor);
  this->state_var_init_types_.push_back("scalar");
  this->state_var_init_values_.push_back(0.0);
  this->state_var_old_state_flags_.push_back(false);
  this->state_var_output_flags_.push_back(true);
  //
  //
  this->num_state_variables_++;
  this->state_var_names_.push_back("alpha");
  this->state_var_layouts_.push_back(dl->qp_scalar);
  this->state_var_init_types_.push_back("scalar");
  this->state_var_init_values_.push_back(0.0);
  this->state_var_old_state_flags_.push_back(true);
  this->state_var_output_flags_.push_back(true);
}
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
void HyperelasticDamageModel<EvalT, Traits>::
computeState(typename Traits::EvalData workset,
    std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT>>> dep_fields,
    std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT>>> eval_fields)
{
  // extract dependent MDFields
  PHX::MDField<ScalarT> def_grad = *dep_fields["F"];
  PHX::MDField<ScalarT> J = *dep_fields["J"];
  PHX::MDField<ScalarT> poissons_ratio = *dep_fields["Poissons Ratio"];
  PHX::MDField<ScalarT> elastic_modulus = *dep_fields["Elastic Modulus"];
  PHX::MDField<ScalarT> delta_time = *dep_fields["Delta Time"];
  // extract evaluated MDFields
  std::string cauchy = (*field_name_map_)["Cauchy_Stress"];
  PHX::MDField<ScalarT> stress = *eval_fields[cauchy];
  PHX::MDField<ScalarT> alpha = *eval_fields["alpha"];
  PHX::MDField<ScalarT> damage;
  if (!have_damage_) {
    damage = *eval_fields["local damage"];
  }
  PHX::MDField<ScalarT> source = *eval_fields["Damage_Source"];
  //get state variables
  Albany::MDArray alpha_old = (*workset.stateArrayPtr)["alpha_old"];
  ScalarT kappa;
  ScalarT mu, mubar;
  ScalarT Jm53, Jm23;
  ScalarT smag;
  ScalarT energy;
  ScalarT dt = delta_time(0);

  Intrepid2::Tensor<ScalarT> F(num_dims_), b(num_dims_), sigma(num_dims_);
  Intrepid2::Tensor<ScalarT> I(Intrepid2::eye<ScalarT>(num_dims_));
  Intrepid2::Tensor<ScalarT> s(num_dims_), n(num_dims_);

  for (int cell(0); cell < workset.numCells; ++cell) {
    for (int pt(0); pt < num_pts_; ++pt) {
      kappa = elastic_modulus(cell, pt)
          / (3. * (1. - 2. * poissons_ratio(cell, pt)));
      mu = elastic_modulus(cell, pt) / (2. * (1. + poissons_ratio(cell, pt)));
      Jm53 = std::pow(J(cell, pt), -5. / 3.);
      Jm23 = Jm53 * J(cell, pt);

      F.fill(def_grad,cell, pt,0,0);
      b = F * transpose(F);
      mubar = (1.0 / 3.0) * mu * Jm23 * Intrepid2::trace(b);
      sigma = 0.5 * kappa * (J(cell, pt) - 1. / J(cell, pt)) * I
          + mu * Jm53 * Intrepid2::dev(b);

      energy = 0.5 * kappa
          * (0.5 * (J(cell, pt) * J(cell, pt) - 1.0) - std::log(J(cell, pt)))
          + 0.5 * mu * (Jm23 * Intrepid2::trace(b) - 3.0);

      if (have_temperature_) {
        ScalarT delta_temp = temperature_(cell, pt) - ref_temperature_;
        energy += heat_capacity_
            * ((delta_temp) - temperature_(cell, pt)
                * std::log(temperature_(cell, pt) / ref_temperature_))
            - 3.0 * expansion_coeff_ * (J(cell, pt) - 1.0 / J(cell, pt))
                * delta_temp;
        sigma -= expansion_coeff_ * (1.0 + 1.0 / (J(cell, pt) * J(cell, pt)))
            * delta_temp * I;
      }

      alpha(cell, pt) = std::max((ScalarT) alpha_old(cell, pt), energy);

      source(cell, pt) = (max_damage_ / damage_saturation_)
          * std::exp(-alpha(cell, pt) / damage_saturation_)
          * (alpha(cell, pt) - alpha_old(cell, pt)) / dt;

      if (!have_damage_) {
        damage(cell, pt) = max_damage_
            * (1.0 - std::exp(-alpha(cell, pt) / damage_saturation_));
      }

      for (int i = 0; i < num_dims_; ++i) {
        for (int j = 0; j < num_dims_; ++j) {
          stress(cell, pt, i, j) = (1.0 - damage(cell, pt)) * sigma(i, j);
        }
      }
    }
  }
}
//------------------------------------------------------------------------------
}

