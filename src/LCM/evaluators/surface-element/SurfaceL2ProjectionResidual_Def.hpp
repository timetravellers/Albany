//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//


#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

#include <Intrepid2_MiniTensor.h>

// THESE NEED TO BE REMOVED!!!
#include "Intrepid2_FunctionSpaceTools.hpp"
#include "Intrepid2_RealSpaceTools.hpp"

namespace LCM {

  //**********************************************************************
  template<typename EvalT, typename Traits>
  SurfaceL2ProjectionResidual<EvalT, Traits>::
  SurfaceL2ProjectionResidual(const Teuchos::ParameterList& p,
                            const Teuchos::RCP<Albany::Layouts>& dl) :
    thickness      (p.get<double>("thickness")),
    cubature       (p.get<Teuchos::RCP<Intrepid2::Cubature<RealType>>>("Cubature")),
    intrepidBasis  (p.get<Teuchos::RCP<Intrepid2::Basis<RealType, Intrepid2::FieldContainer<RealType>>>>("Intrepid2 Basis")),
    surface_Grad_BF     (p.get<std::string>("Surface Scalar Gradient Operator Name"),dl->node_qp_gradient),
    refDualBasis   (p.get<std::string>("Reference Dual Basis Name"),dl->qp_tensor),
    refNormal      (p.get<std::string>("Reference Normal Name"),dl->qp_vector),
    refArea        (p.get<std::string>("Reference Area Name"),dl->qp_scalar),
    Cauchy_stress_   (p.get<std::string>("Cauchy Stress Name"),dl->qp_tensor),
    detF_       (p.get<std::string>("Jacobian Name"),dl->qp_scalar),
    projected_tau_          (p.get<std::string>("HydoStress Name"),dl->qp_scalar),
    projection_residual_ (p.get<std::string>("Residual Name"),dl->node_scalar)
  {
    this->addDependentField(surface_Grad_BF);
    this->addDependentField(refDualBasis);
    this->addDependentField(refNormal);    
    this->addDependentField(refArea);
    this->addDependentField(detF_);
    this->addDependentField(Cauchy_stress_);
    this->addDependentField(projected_tau_);

    this->addEvaluatedField(projection_residual_);

    this->setName("HydroStress Residual"+PHX::typeAsString<EvalT>());

    std::vector<PHX::DataLayout::size_type> dims;
    dl->node_vector->dimensions(dims);
    worksetSize = dims[0];
    numNodes = dims[1];
    numDims = dims[2];

    numQPs = cubature->getNumPoints();

    numPlaneNodes = numNodes / 2;
    numPlaneDims = numDims - 1;

#ifdef ALBANY_VERBOSE
    std::cout << "in Surface Scalar Residual" << std::endl;
    std::cout << " numPlaneNodes: " << numPlaneNodes << std::endl;
    std::cout << " numPlaneDims: " << numPlaneDims << std::endl;
    std::cout << " numQPs: " << numQPs << std::endl;
    std::cout << " cubature->getNumPoints(): " << cubature->getNumPoints() << std::endl;
    std::cout << " cubature->getDimension(): " << cubature->getDimension() << std::endl;
#endif

    // Allocate Temporary FieldContainers
    refValues.resize(numPlaneNodes, numQPs);
    refGrads.resize(numPlaneNodes, numQPs, numPlaneDims);
    refPoints.resize(numQPs, numPlaneDims);
    refWeights.resize(numQPs);

    // Pre-Calculate reference element quantitites
    cubature->getCubature(refPoints, refWeights);
    intrepidBasis->getValues(refValues, refPoints, Intrepid2::OPERATOR_VALUE);
    intrepidBasis->getValues(refGrads, refPoints, Intrepid2::OPERATOR_GRAD);

  }

  //**********************************************************************
  template<typename EvalT, typename Traits>
  void SurfaceL2ProjectionResidual<EvalT, Traits>::
  postRegistrationSetup(typename Traits::SetupData d,
                        PHX::FieldManager<Traits>& fm)
  {
    this->utils.setFieldData(surface_Grad_BF,fm);
    this->utils.setFieldData(refDualBasis,fm);
    this->utils.setFieldData(refNormal,fm);
    this->utils.setFieldData(refArea,fm);
    this->utils.setFieldData(projected_tau_, fm);
    this->utils.setFieldData(projection_residual_,fm);
    //NOTE: those are in surface elements
    this->utils.setFieldData(Cauchy_stress_,fm);
    this->utils.setFieldData(detF_,fm);

  }

  //**********************************************************************
  template<typename EvalT, typename Traits>
  void SurfaceL2ProjectionResidual<EvalT, Traits>::
  evaluateFields(typename Traits::EvalData workset)
  {
    // THESE NEED TO BE REMOVED!!!
    typedef Intrepid2::FunctionSpaceTools FST;
    typedef Intrepid2::RealSpaceTools<ScalarT> RST;

    ScalarT tau(0);

   // Initialize the residual
    for (int cell(0); cell < workset.numCells; ++cell) {
      for (int node(0); node < numPlaneNodes; ++node) {
        int topNode = node + numPlaneNodes;
        	 projection_residual_(cell, node) = 0;
        	 projection_residual_(cell, topNode) = 0;
      }
    }

    for (int cell(0); cell < workset.numCells; ++cell) {
      for (int node(0); node < numPlaneNodes; ++node) {
        int topNode = node + numPlaneNodes;
        	 for (int pt=0; pt < numQPs; ++pt) {
        		 tau = 0.0;
 
                 for (int dim=0; dim <numDims; ++dim){
                	 tau += detF_(cell,pt)*Cauchy_stress_(cell, pt, dim, dim)/numDims;
                 }

        	  projection_residual_(cell, node) += refValues(node,pt)*
                	       	                            (projected_tau_(cell,pt) -  tau)*
                                                               refArea(cell,pt)*thickness;

        	 }
        	 projection_residual_(cell, topNode) =  projection_residual_(cell, node);

      }
    }

  }
  //**********************************************************************  
}
