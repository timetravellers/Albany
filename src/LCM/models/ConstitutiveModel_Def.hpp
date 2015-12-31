//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include <Intrepid2_MiniTensor.h>
#include <Phalanx_DataLayout.hpp>
#include <Teuchos_TestForException.hpp>

namespace LCM
{

//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
ConstitutiveModel<EvalT, Traits>::
ConstitutiveModel(Teuchos::ParameterList* p,
    const Teuchos::RCP<Albany::Layouts>& dl) :
    num_state_variables_(0),
    compute_energy_(false),
    compute_tangent_(false),
    need_integration_pt_locations_(false),
    have_temperature_(false),
    have_damage_(false),
    have_total_concentration_(false),
    have_total_bubble_density_(false),
    have_bubble_volume_fraction_(false)
{
  // extract number of integration points and dimensions
  std::vector<PHX::DataLayout::size_type> dims;
  dl->qp_tensor->dimensions(dims);
  num_pts_  = dims[1];
  num_dims_ = dims[2];
  field_name_map_ =
      p->get<Teuchos::RCP<std::map<std::string, std::string>>>("Name Map");

  if (p->isType<bool>("Have Temperature")) {
    if (p->get<bool>("Have Temperature")) {
      have_temperature_ = true;
      expansion_coeff_ = p->get<RealType>("Thermal Expansion Coefficient", 0.0);
      ref_temperature_ = p->get<RealType>("Reference Temperature", 0.0);
      heat_capacity_ = p->get<RealType>("Heat Capacity", 1.0);
      density_ = p->get<RealType>("Density", 1.0);
    }
  }

  if (p->isType<bool>("Have Damage")) {
    if (p->get<bool>("Have Damage")) {
      have_damage_ = true;
    }
  }

  if (p->isType<bool>("Have Total Concentration")) {
    if (p->get<bool>("Have Total Concentration")) {
      have_total_concentration_ = true;
    }
  }

  if (p->isType<bool>("Have Total Bubble Density")) {
    if (p->get<bool>("Have Total Bubble Density")) {
      have_total_bubble_density_ = true;
    }
  }

  if (p->isType<bool>("Have Bubble Volume Fraction")) {
    if (p->get<bool>("Have Bubble Volume Fraction")) {
      have_bubble_volume_fraction_ = true;
    }
  }

  if (p->isType<bool>("Compute Tangent")) {
    if (p->get<bool>("Compute Tangent")) {
      compute_tangent_ = true;
    }
  }

}
//------------------------------------------------------------------------------
////Kokkos Kernel for computeVolumeAverage
template < typename ScalarT, class ArrayStress, class ArrayWeights, class ArrayJ >
class computeVolumeAverageKernel {
 ArrayStress stress;
 const ArrayWeights weights_;
 const ArrayJ j_;
 int num_pts_, num_dims_; 


 public:
 typedef PHX::Device device_type;

 computeVolumeAverageKernel( ArrayStress &stress_,
                             const ArrayWeights &weights,
                             const ArrayJ &j,
                             const int num_pts,
                             const int num_dims)
                           : stress(stress_)
                           , weights_(weights)
                           , j_(j)
                           , num_pts_(num_pts)
                           , num_dims_(num_dims){}
 
 KOKKOS_INLINE_FUNCTION
 void operator () (const int cell) const
 {
#ifndef PHX_KOKKOS_DEVICE_TYPE_CUDA
    ScalarT volume, pbar, p;
    Intrepid2::Tensor<ScalarT> sig(num_dims_);
    Intrepid2::Tensor<ScalarT> I(Intrepid2::eye<ScalarT>(num_dims_));

    volume = pbar = 0.0;

    for (int pt(0); pt < num_pts_; ++pt) {

     for (int i = 0; i < num_dims_; ++i)
       for (int j = 0; j < num_dims_; ++j)
         sig(i,j)=stress(cell,pt,i,j);

      pbar += weights_(cell,pt) * (1./num_dims_) * Intrepid2::trace(sig);
      volume += weights_(cell,pt) * j_(cell,pt);
    }

    pbar /= volume;

    for (int pt(0); pt < num_pts_; ++pt) {
 
     for (int i = 0; i < num_dims_; ++i)
       for (int j = 0; j < num_dims_; ++j)
         sig(i,j)=stress(cell,pt,i,j);     

      p = (1./num_dims_) * Intrepid2::trace(sig);
      sig += (pbar - p)*I;

      for (int i = 0; i < num_dims_; ++i) {
        stress(cell,pt,i,i) = sig(i,i);
      }
    }
#else
  ScalarT volume, pbar, p;
    ScalarT sig[3][3];
    ScalarT I[3][3];
 
    ScalarT trace_sig=0.0;   

    if (num_dims_>3) 
          Kokkos::abort( "Error: ConstitutiveModel::computeVolumeAverage: size of temorary array is smaller then it should be"); 
 
   for (int i=0; i<num_dims_; i++){
      for (int j=0; j<num_dims_; j++){
        I[i][j]=ScalarT(0.0);  
        if (i==j)
           I[i][j]=ScalarT(1.0);
      }
   }

    volume =0.0;
     pbar = 0.0;

    for (int pt(0); pt < num_pts_; ++pt) {

     for (int i = 0; i < num_dims_; ++i)
       for (int j = 0; j < num_dims_; ++j)
         sig[i][j]=stress(cell,pt,i,j);

      trace_sig=0.0;

     for (int i = 0; i < num_dims_; ++i) {
        trace_sig += sig[i][i];


      pbar += weights_(cell,pt) * (1./num_dims_) * trace_sig;
      volume += weights_(cell,pt) * j_(cell,pt);
    }
   }

    pbar /= volume;

    for (int pt(0); pt < num_pts_; ++pt) {
 
     for (int i = 0; i < num_dims_; ++i)
       for (int j = 0; j < num_dims_; ++j)
         sig[i][j]=stress(cell,pt,i,j);     
      
      
      trace_sig=0.0;

      for (int i = 0; i < num_dims_; ++i) {
        trace_sig += sig[i][i];

      p = (1./num_dims_) * trace_sig;
     
      for (int i = 0; i < num_dims_; ++i)
       for (int j = 0; j < num_dims_; ++j)
         sig[i][j]+=(pbar - p)*I[i][j];
      //sig += (pbar - p)*I;

      for (int i = 0; i < num_dims_; ++i) {
        stress(cell,pt,i,i) = sig[i][i];
      }
    }

 }
#endif
}
};
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
void ConstitutiveModel<EvalT, Traits>::
computeVolumeAverage(typename Traits::EvalData workset,
    std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT>>> dep_fields,
    std::map<std::string, Teuchos::RCP<PHX::MDField<ScalarT>>> eval_fields)
{

  std::string cauchy = (*field_name_map_)["Cauchy_Stress"];
  PHX::MDField<ScalarT> stress = *eval_fields[cauchy];
#ifndef ALBANY_KOKKOS_UNDER_DEVELOPMENT
  Intrepid2::Tensor<ScalarT> sig(num_dims_);
  Intrepid2::Tensor<ScalarT> I(Intrepid2::eye<ScalarT>(num_dims_));

  ScalarT volume, pbar, p;

  for (int cell(0); cell < workset.numCells; ++cell) {
    volume = pbar = 0.0;
    for (int pt(0); pt < num_pts_; ++pt) {
      sig.fill(stress,cell,pt,0,0);
      pbar += weights_(cell,pt) * (1./num_dims_) * Intrepid2::trace(sig);
      volume += weights_(cell,pt) * j_(cell,pt);
    }

    pbar /= volume;

    for (int pt(0); pt < num_pts_; ++pt) {
      sig.fill(stress,cell,pt,0,0);
      p = (1./num_dims_) * Intrepid2::trace(sig);
      sig += (pbar - p)*I;

      for (int i = 0; i < num_dims_; ++i) {
        stress(cell,pt,i,i) = sig(i,i);
      }
    }
  }
#else
  Kokkos::parallel_for(workset.numCells, computeVolumeAverageKernel<ScalarT, PHX::MDField<ScalarT>, PHX::MDField<MeshScalarT, Cell, QuadPoint>, PHX::MDField<ScalarT, Cell, QuadPoint>>(stress, weights_, j_, num_pts_, num_dims_));
#endif
}
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
std::string ConstitutiveModel<EvalT, Traits>::
getStateVarName(int state_var)
{
  return state_var_names_[state_var];
}
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
Teuchos::RCP<PHX::DataLayout> ConstitutiveModel<EvalT, Traits>::
getStateVarLayout(int state_var)
{
  return state_var_layouts_[state_var];
}
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
std::string ConstitutiveModel<EvalT, Traits>::
getStateVarInitType(int state_var)
{
  return state_var_init_types_[state_var];
}
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
double ConstitutiveModel<EvalT, Traits>::
getStateVarInitValue(int state_var)
{
  return state_var_init_values_[state_var];
}
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
bool ConstitutiveModel<EvalT, Traits>::
getStateVarOldStateFlag(int state_var)
{
  return state_var_old_state_flags_[state_var];
}
//------------------------------------------------------------------------------
template<typename EvalT, typename Traits>
bool ConstitutiveModel<EvalT, Traits>::
getStateVarOutputFlag(int state_var)
{
  return state_var_output_flags_[state_var];
}
//------------------------------------------------------------------------------
}

