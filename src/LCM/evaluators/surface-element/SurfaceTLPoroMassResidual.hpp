//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//


#ifndef SURFACE_TL_PORO_MASS_RESIDUAL_HPP
#define SURFACE_TL_PORO_MASS_RESIDUAL_HPP

#include "Phalanx_config.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"
#include "Intrepid2_CellTools.hpp"
#include "Intrepid2_Cubature.hpp"

#include "Albany_Layouts.hpp"

namespace LCM {
/** \brief

    Compute the balance of mass residual on the surface

**/

template<typename EvalT, typename Traits>
class SurfaceTLPoroMassResidual : public PHX::EvaluatorWithBaseImpl<Traits>,
                              public PHX::EvaluatorDerived<EvalT, Traits>  {

public:

  SurfaceTLPoroMassResidual(const Teuchos::ParameterList& p,
                        const Teuchos::RCP<Albany::Layouts>& dl);

  void postRegistrationSetup(typename Traits::SetupData d,
			     PHX::FieldManager<Traits>& vm);

  void evaluateFields(typename Traits::EvalData d);

private:

  typedef typename EvalT::ScalarT ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;


  // Input:
  //! Length scale parameter for localization zone
  ScalarT thickness;
  //! Numerical integration rule
  Teuchos::RCP<Intrepid2::Cubature<RealType>> cubature;
  //! Finite element basis for the midplane
  Teuchos::RCP<Intrepid2::Basis<RealType, Intrepid2::FieldContainer<RealType>>> intrepidBasis;
  //! Scalar Gradient
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim> scalarGrad;
 //! Scalar Gradient Operator
  PHX::MDField<MeshScalarT,Cell,Node,QuadPoint,Dim> surface_Grad_BF;
  //! Scalar Jump
   PHX::MDField<ScalarT,Cell,QuadPoint> scalarJump;
  //! Reference configuration dual basis
  PHX::MDField<MeshScalarT,Cell,QuadPoint,Dim, Dim> refDualBasis;
  //! Reference configuration normal
  PHX::MDField<MeshScalarT,Cell,QuadPoint,Dim> refNormal;
  //! Reference configuration area
  PHX::MDField<MeshScalarT,Cell,QuadPoint> refArea;
  //! Determinant of the surface deformation gradient
  PHX::MDField<ScalarT,Cell,QuadPoint> J;
  //! Pore Pressure at the 2D integration point location
  PHX::MDField<ScalarT,Cell,QuadPoint> porePressure;
  //! Nodal Pore Pressure at the 2D integration point location
  PHX::MDField<ScalarT,Cell,Node> nodalPorePressure;
  //! Biot Coefficient at the 2D integration point location
  PHX::MDField<ScalarT,Cell,QuadPoint> biotCoefficient;
  //! Biot Modulus at the 2D integration point location
  PHX::MDField<ScalarT,Cell,QuadPoint> biotModulus;
  //! Permeability at the 2D integration point location
  PHX::MDField<ScalarT,Cell,QuadPoint> kcPermeability;
  //! Deformation Gradient
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim, Dim> defGrad;

//  // weight times basis function value at integration point
//  PHX::MDField<MeshScalarT,Cell,Node,QuadPoint> wBF;

  //Data from previous time step
   std::string porePressureName, JName;

   // Time
   PHX::MDField<ScalarT,Dummy> deltaTime;

  //! Reference Cell FieldContainers
  Intrepid2::FieldContainer<RealType> refValues;
  Intrepid2::FieldContainer<RealType> refGrads;
  Intrepid2::FieldContainer<RealType> refPoints;
  Intrepid2::FieldContainer<RealType> refWeights;

  // Work space FCs
  Intrepid2::FieldContainer<ScalarT> F_inv;
  Intrepid2::FieldContainer<ScalarT> F_invT;
  Intrepid2::FieldContainer<ScalarT> C;
  Intrepid2::FieldContainer<ScalarT> Cinv;
  Intrepid2::FieldContainer<ScalarT> JF_invT;
  Intrepid2::FieldContainer<ScalarT> KJF_invT;
  Intrepid2::FieldContainer<ScalarT> Kref;

  // Temporary FieldContainers
  Intrepid2::FieldContainer<ScalarT> flux;

  // Output:
  PHX::MDField<ScalarT,Cell,Node> poroMassResidual;

  unsigned int worksetSize;
  unsigned int numNodes;
  unsigned int numQPs;
  unsigned int numDims;
  unsigned int numPlaneNodes;
  unsigned int numPlaneDims;

  bool haveMech;
};
}

#endif
