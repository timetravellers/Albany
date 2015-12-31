//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef SURFACESCALARGRADIENT_HPP
#define SURFACESCALARGRADIENT_HPP

#include "Phalanx_config.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"
#include "Intrepid2_CellTools.hpp"
#include "Intrepid2_Cubature.hpp"

#include "Albany_Layouts.hpp"

namespace LCM {
/** \brief

    Construct a scalar gradient on a surface

**/

template<typename EvalT, typename Traits>
class SurfaceScalarGradient : public PHX::EvaluatorWithBaseImpl<Traits>,
                          public PHX::EvaluatorDerived<EvalT, Traits>  {

public:



  SurfaceScalarGradient(const Teuchos::ParameterList& p,
                        const Teuchos::RCP<Albany::Layouts>& dl);

  void postRegistrationSetup(typename Traits::SetupData d,
			     PHX::FieldManager<Traits>& vm);

  void evaluateFields(typename Traits::EvalData d);



private:

  typedef typename EvalT::ScalarT ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;

  // Input:
  /// Length scale parameter for localization zone
  ScalarT thickness;
  /// Numerical integration rule
  Teuchos::RCP<Intrepid2::Cubature<RealType>> cubature;

  /// for the parallel gradient term
  Teuchos::RCP<Intrepid2::Basis<RealType, Intrepid2::FieldContainer<RealType>>> intrepidBasis;
  // nodal value used to construct in-plan gradient
  PHX::MDField<ScalarT,Cell,Node> nodalScalar;

  //! Vector to take the jump of
  PHX::MDField<ScalarT,Cell,QuadPoint> jump;

  PHX::MDField<MeshScalarT,Cell,QuadPoint,Dim, Dim> refDualBasis;
  PHX::MDField<MeshScalarT,Cell,QuadPoint,Dim> refNormal;

  //! Reference Cell FieldContainers
  Intrepid2::FieldContainer<RealType> refValues;
  Intrepid2::FieldContainer<RealType> refGrads;
  Intrepid2::FieldContainer<RealType> refPoints;
  Intrepid2::FieldContainer<RealType> refWeights;
  // Output:
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim> scalarGrad;

  unsigned int worksetSize;
  unsigned int numNodes;
  unsigned int numQPs;
  unsigned int numDims;
  unsigned int numPlaneNodes;
  unsigned int numPlaneDims;
};
}

#endif
