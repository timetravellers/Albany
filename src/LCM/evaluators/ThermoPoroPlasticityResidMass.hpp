//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef THERMOPOROPLASTICITYRESIDMASS_HPP
#define THERMOPOROPLASTICITYRESIDMASS_HPP

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
class ThermoPoroPlasticityResidMass : public PHX::EvaluatorWithBaseImpl<Traits>,
				public PHX::EvaluatorDerived<EvalT, Traits>  {

public:

  ThermoPoroPlasticityResidMass(const Teuchos::ParameterList& p);

  void postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& vm);

  void evaluateFields(typename Traits::EvalData d);

private:

  typedef typename EvalT::ScalarT ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;

  // Input:
  PHX::MDField<MeshScalarT,Cell,Node,QuadPoint> wBF;
  PHX::MDField<ScalarT,Cell,QuadPoint> porePressure;
  PHX::MDField<ScalarT,Cell,QuadPoint> densityPoreFluid;
  PHX::MDField<ScalarT,Cell,QuadPoint> Temp;
  PHX::MDField<ScalarT,Cell,QuadPoint> RefTemp;
  PHX::MDField<ScalarT,Cell,QuadPoint> stabParameter;
  PHX::MDField<ScalarT,Cell,QuadPoint> ThermalCond;
  PHX::MDField<ScalarT,Cell,QuadPoint> kcPermeability;
  PHX::MDField<ScalarT,Cell,QuadPoint> porosity;
  PHX::MDField<ScalarT,Cell,QuadPoint> biotCoefficient;
  PHX::MDField<ScalarT,Cell,QuadPoint> biotModulus;
  PHX::MDField<ScalarT,Cell,QuadPoint> young_modulus_;
  PHX::MDField<ScalarT,Cell,QuadPoint> poissons_ratio_;
  PHX::MDField<MeshScalarT,Cell,Node,QuadPoint,Dim> wGradBF;
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim> TGrad;
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim> TempGrad;
  PHX::MDField<ScalarT,Cell,QuadPoint> alphaMixture;
  PHX::MDField<ScalarT,Cell,QuadPoint> alphaPoreFluid;
  PHX::MDField<ScalarT,Cell,QuadPoint> alphaSkeleton;
  PHX::MDField<ScalarT,Cell,QuadPoint> Source;
  Teuchos::Array<double> convectionVels;
  PHX::MDField<ScalarT,Cell,QuadPoint> rhoCp;
  PHX::MDField<ScalarT,Cell,QuadPoint> Absorption;
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim> strain;
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim> defgrad;
  PHX::MDField<ScalarT,Cell,QuadPoint> J;

  // stabilization term
  PHX::MDField<MeshScalarT,Cell,Vertex,Dim> coordVec;
  Teuchos::RCP<Intrepid2::Cubature<RealType>> cubature;
  Teuchos::RCP<shards::CellTopology> cellType;
  PHX::MDField<MeshScalarT,Cell,QuadPoint> weights;

  // Time
  PHX::MDField<ScalarT,Dummy> deltaTime;

  //Data from previous time step
  std::string strainName, porePressureName, porosityName,
              JName, TempName;



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
  Intrepid2::FieldContainer<ScalarT> flux;
  Intrepid2::FieldContainer<ScalarT> fgravity;
  Intrepid2::FieldContainer<ScalarT> fluxdt;
  Intrepid2::FieldContainer<ScalarT> pterm;
  Intrepid2::FieldContainer<ScalarT> Tterm;
  Intrepid2::FieldContainer<ScalarT> aterm;
  Intrepid2::FieldContainer<ScalarT> tpterm;

  // Temporary FieldContainers
  Intrepid2::FieldContainer<RealType> refPoints;
  Intrepid2::FieldContainer<RealType> refWeights;
  Intrepid2::FieldContainer<MeshScalarT> jacobian;
  Intrepid2::FieldContainer<MeshScalarT> jacobian_inv;
  Intrepid2::FieldContainer<MeshScalarT> Gc;

  // Work space FCs
  Intrepid2::FieldContainer<ScalarT> F_inv;
  Intrepid2::FieldContainer<ScalarT> F_invT;
  Intrepid2::FieldContainer<ScalarT> C;
  Intrepid2::FieldContainer<ScalarT> Cinv;
  Intrepid2::FieldContainer<ScalarT> JF_invT;
  Intrepid2::FieldContainer<ScalarT> KJF_invT;
  Intrepid2::FieldContainer<ScalarT> Kref;

  ScalarT porePbar, Tempbar, vol;
  ScalarT trialPbar;
  ScalarT shearModulus, bulkModulus;
  ScalarT safeFactor;



  // Output:
  PHX::MDField<ScalarT,Cell,Node> TResidual;


};
}

#endif
