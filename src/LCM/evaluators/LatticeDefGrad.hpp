//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef LATTICEDEFGRAD_HPP
#define LATTICEDEFGRAD_HPP

#include "Phalanx_config.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"

#include "Intrepid2_CellTools.hpp"
#include "Intrepid2_Cubature.hpp"

namespace LCM {
/** \brief Lattice Deformation Gradient

    This evaluator computes the hydrogen induced multiplicative decomposition
    of deformation gradient


*/

template<typename EvalT, typename Traits>
class LatticeDefGrad : public PHX::EvaluatorWithBaseImpl<Traits>,
		public PHX::EvaluatorDerived<EvalT, Traits>  {

public:

  LatticeDefGrad(const Teuchos::ParameterList& p);

  void postRegistrationSetup(typename Traits::SetupData d,
			     PHX::FieldManager<Traits>& vm);

  void evaluateFields(typename Traits::EvalData d);

private:

  typedef typename EvalT::ScalarT ScalarT;
  typedef typename EvalT::MeshScalarT MeshScalarT;

  // Input:
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim> defgrad;
  PHX::MDField<ScalarT,Cell,QuadPoint> J;
  PHX::MDField<ScalarT,Cell,QuadPoint> JH;
  PHX::MDField<ScalarT,Cell,QuadPoint> VH; // partial molar volume
  PHX::MDField<ScalarT,Cell,QuadPoint> VM; // molar volume of Fe
  PHX::MDField<ScalarT,Cell,QuadPoint> CtotalRef; // stress free concentration
  PHX::MDField<ScalarT,Cell,QuadPoint> Ctotal; // current total concentration
  PHX::MDField<MeshScalarT,Cell,QuadPoint> weights;

  // Output:
  PHX::MDField<ScalarT,Cell,QuadPoint,Dim,Dim> latticeDefGrad;


  unsigned int numQPs;
  unsigned int numDims;
  unsigned int worksetSize;

  //! flag to compute the weighted average of J
  bool weightedAverage;

  //! stabilization parameter for the weighted average
  ScalarT alpha;
  

};

}
#endif
