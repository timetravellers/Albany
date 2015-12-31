//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef LAPLACERESID_HPP
#define LAPLACERESID_HPP

#include "Phalanx_config.hpp"
#include "Phalanx_Evaluator_WithBaseImpl.hpp"
#include "Phalanx_Evaluator_Derived.hpp"
#include "Phalanx_MDField.hpp"

#include "Albany_Layouts.hpp"
#include "Intrepid2_CellTools.hpp"
#include "Intrepid2_Cubature.hpp"

namespace PHAL {

/** \brief Finite Element Interpolation Evaluator

    This evaluator interpolates nodal DOF values to quad points.

*/

template<typename EvalT, typename Traits>
class LaplaceResid : public PHX::EvaluatorWithBaseImpl<Traits>,
  public PHX::EvaluatorDerived<EvalT, Traits>  {

  public:

    LaplaceResid(const Teuchos::ParameterList& p,
                         const Teuchos::RCP<Albany::Layouts>& dl);

    void postRegistrationSetup(typename Traits::SetupData d,
                               PHX::FieldManager<Traits>& vm);

    void evaluateFields(typename Traits::EvalData d);

  private:

    typedef typename EvalT::MeshScalarT MeshScalarT;
    typedef typename EvalT::ScalarT ScalarT;

    // Input:
    //! Coordinate vector at vertices
    PHX::MDField<MeshScalarT, Cell, Vertex, Dim> coordVec;

    //! Coordinate vector at vertices being solved for
    PHX::MDField<ScalarT, Cell, Node, Dim> solnVec;

    Teuchos::RCP<Intrepid2::Cubature<RealType> > cubature;
    Teuchos::RCP<shards::CellTopology> cellType;
    Teuchos::RCP<Intrepid2::Basis<RealType, Intrepid2::FieldContainer<RealType> > > intrepidBasis;

    // Temporary FieldContainers
    Intrepid2::FieldContainer<RealType> grad_at_cub_points;
    Intrepid2::FieldContainer<RealType> refPoints;
    Intrepid2::FieldContainer<RealType> refWeights;
    Intrepid2::FieldContainer<MeshScalarT> jacobian;
    Intrepid2::FieldContainer<MeshScalarT> jacobian_det;

    // Output:
    PHX::MDField<ScalarT, Cell, Node, Dim> solnResidual;

    unsigned int numQPs, numDims, numNodes, worksetSize;

};
}

#endif
