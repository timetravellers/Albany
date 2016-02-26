//*****************************************************************//
//    Albany 3.0:  Copyright 2016 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef THERMOPOROPLASTICITYRESIDENERGY_HPP
#define THERMOPOROPLASTICITYRESIDENERGY_HPP

#include "Phalanx_config.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"

#include "Intrepid2_CellTools.hpp"
#include "Intrepid2_Cubature.hpp"

namespace LCM {
/** \brief
 *   Balance of energy residual for large deformation
 *   thermoporomechanics problem.

*/

template<typename EvalT, typename Traits>
class ThermoPoroPlasticityResidEnergy : public PHX::EvaluatorWithBaseImpl<Traits>,
				public PHX::EvaluatorDerived<EvalT, Traits>  {

public:

  ThermoPoroPlasticityResidEnergy(const Teuchos::ParameterList& p);

  void postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& vm);

  void evaluateFields(typename Traits::EvalData d);

private:

  typedef typename EvalT::ScalarT ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;

  // Input:
  PHX::MDField<MeshScalarT,Cell,Node,QuadPoint> wBF;
  PHX::MDField<ScalarT,Cell,QuadPoint> porePressure;
  PHX::MDField<ScalarT,Cell,QuadPoint> refTemp;
  PHX::MDField<ScalarT,Cell,QuadPoint> alphaSkeleton;
  PHX::MDField<ScalarT,Cell,QuadPoint> gammaMixture;
  PHX::MDField<ScalarT,Cell,QuadPoint> gammaFluid;
  PHX::MDField<ScalarT,Cell,QuadPoint> Temp;
  PHX::MDField<ScalarT,Cell,QuadPoint> stabParameter;
  PHX::MDField<ScalarT,Cell,QuadPoint> ThermalCond;
  PHX::MDField<ScalarT,Cell,QuadPoint> kcPermeability;
  PHX::MDField<ScalarT,Cell,QuadPoint> porosity;
  PHX::MDField<ScalarT,Cell,QuadPoint> biotCoefficient;
  PHX::MDField<ScalarT,Cell,QuadPoint> biotModulus;
  PHX::MDField<ScalarT,Cell,QuadPoint> bulk;
  PHX::MDField<MeshScalarT,Cell,Node,QuadPoint,Dim> wGradBF;
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim> TGrad;
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim> PGrad;
  PHX::MDField<ScalarT,Cell,QuadPoint> alphaMixture;
  PHX::MDField<ScalarT,Cell,QuadPoint> Source;
  Teuchos::Array<double> convectionVels;
  PHX::MDField<ScalarT,Cell,QuadPoint> rhoCp;
  PHX::MDField<ScalarT,Cell,QuadPoint> Absorption;
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim> strain;

  PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim> defgrad;
  PHX::MDField<ScalarT,Cell,QuadPoint> J;


  // stabilization term
  PHX::MDField<MeshScalarT,Cell,Vertex,Dim> coordVec;
  Teuchos::RCP<Intrepid2::Cubature<RealType, Intrepid2::FieldContainer_Kokkos<RealType, PHX::Layout,PHX::Device> >> cubature;
  Teuchos::RCP<shards::CellTopology> cellType;
  PHX::MDField<MeshScalarT,Cell,QuadPoint> weights;

  PHX::MDField<ScalarT,Cell,QuadPoint> young_modulus_;
  PHX::MDField<ScalarT,Cell,QuadPoint> poissons_ratio_;

  // Time
  PHX::MDField<ScalarT,Dummy> deltaTime;

  //Data from previous time step
  std::string strainName, porePressureName, porosityName,
              JName, tempName;



  bool haveSource;
  bool haveConvection;
  bool haveAbsorption;
  bool enableTransient;
  bool haverhoCp;
  unsigned int numNodes;
  unsigned int numQPs;
  unsigned int numDims;
  unsigned int worksetSize;

  // Temporary FieldContainers
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> flux;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> fluxdt;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> pterm;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> aterm;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> tterm;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> tpterm;
  // Temporary FieldContainers
  Intrepid2::FieldContainer_Kokkos<RealType, PHX::Layout, PHX::Device> refPoints;
  Intrepid2::FieldContainer_Kokkos<RealType, PHX::Layout, PHX::Device> refWeights;
  Intrepid2::FieldContainer_Kokkos<MeshScalarT, PHX::Layout, PHX::Device> jacobian;
  Intrepid2::FieldContainer_Kokkos<MeshScalarT, PHX::Layout, PHX::Device> jacobian_inv;
  Intrepid2::FieldContainer_Kokkos<MeshScalarT, PHX::Layout, PHX::Device> Gc;

  // Work space FCs
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> F_inv;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> F_invT;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> C;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> Cinv;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> JF_invT;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> KJF_invT;
  Intrepid2::FieldContainer_Kokkos<ScalarT, PHX::Layout, PHX::Device> Kref;

  ScalarT porePbar, Tempbar, vol, corrTerm;
  ScalarT trialPbar;

  ScalarT shearModulus, bulkModulus;
  ScalarT safeFactor;

  // Output:
  PHX::MDField<ScalarT,Cell,Node> TResidual;


};
}

#endif
