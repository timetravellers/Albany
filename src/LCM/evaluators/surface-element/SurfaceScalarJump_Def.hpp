//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"

namespace LCM {

//**********************************************************************
template<typename EvalT, typename Traits>
SurfaceScalarJump<EvalT, Traits>::
SurfaceScalarJump(const Teuchos::ParameterList& p,
                  const Teuchos::RCP<Albany::Layouts>& dl) :
  cubature      (p.get<Teuchos::RCP<Intrepid2::Cubature<RealType>>>("Cubature")), 
  intrepidBasis (p.get<Teuchos::RCP<Intrepid2::Basis<RealType, Intrepid2::FieldContainer<RealType>>>>("Intrepid2 Basis"))
//  scalar        (p.get<std::string>("Nodal Scalar Name"),dl->node_scalar),
//  scalarJump    (p.get<std::string>("Scalar Jump Name"),dl->qp_scalar),
 // scalarAverage (p.get<std::string>("Scalar Average Name"),dl->qp_scalar)
{
 // this->addDependentField(scalar);

//  this->addEvaluatedField(scalarJump);
//  this->addEvaluatedField(scalarAverage);

  this->setName("Surface Scalar Jump"+PHX::typeAsString<EvalT>());

  haveTemperature = false;
  haveTransport = false;
  haveHydroStress = false;
  havePorePressure = false;

  if (p.isType<std::string>("Nodal Pore Pressure Name")) {
	havePorePressure = true;
    PHX::MDField<ScalarT,Cell,Vertex>
      tp(p.get<std::string>("Nodal Pore Pressure Name"), dl->node_scalar);
    nodalPorePressure = tp;
    this->addDependentField(nodalPorePressure);

    PHX::MDField<ScalarT,Cell,QuadPoint>
      tjp(p.get<std::string>("Jump of Pore Pressure Name"), dl->qp_scalar);
    jumpPorePressure = tjp;
    this->addEvaluatedField(jumpPorePressure);

    PHX::MDField<ScalarT,Cell,QuadPoint>
      tmpp(p.get<std::string>("MidPlane Pore Pressure Name"), dl->qp_scalar);
    midPlanePorePressure = tmpp;
    this->addEvaluatedField(midPlanePorePressure);
  }

  if (p.isType<std::string>("Nodal Temperature Name")) {
	haveTemperature = true;
    PHX::MDField<ScalarT,Cell,Vertex>
      tf(p.get<std::string>("Nodal Temperature Name"), dl->node_scalar);
    nodalTemperature = tf;
    this->addDependentField(nodalTemperature);

    PHX::MDField<ScalarT,Cell,QuadPoint>
      tt(p.get<std::string>("Jump of Temperature Name"), dl->qp_scalar);
    jumpTemperature = tt;
    this->addEvaluatedField(jumpTemperature);

    PHX::MDField<ScalarT,Cell,QuadPoint>
      tmt(p.get<std::string>("MidPlane Temperature Name"), dl->qp_scalar);
    midPlaneTemperature = tmt;
    this->addEvaluatedField(midPlaneTemperature);
  }

  if (p.isType<std::string>("Nodal Transport Name")) {
	haveTransport = true;
    PHX::MDField<ScalarT,Cell,Vertex>
      ttp(p.get<std::string>("Nodal Transport Name"), dl->node_scalar);
    nodalTransport = ttp;
    this->addDependentField(nodalTransport);

    PHX::MDField<ScalarT,Cell,QuadPoint>
    tjtp(p.get<std::string>("Jump of Transport Name"), dl->qp_scalar);
    jumpTransport= tjtp;
    this->addEvaluatedField(jumpTransport);

    PHX::MDField<ScalarT,Cell,QuadPoint>
    tjtm(p.get<std::string>("MidPlane Transport Name"), dl->qp_scalar);
    midPlaneTransport = tjtm;
    this->addEvaluatedField(midPlaneTransport);
  }

  if (p.isType<std::string>("Nodal HydroStress Name")) {

    haveHydroStress = true;
    PHX::MDField<ScalarT,Cell,Vertex>
      ths(p.get<std::string>("Nodal HydroStress Name"), dl->node_scalar);
    nodalHydroStress = ths;
    this->addDependentField(nodalHydroStress);

    PHX::MDField<ScalarT,Cell,QuadPoint>
    tjths(p.get<std::string>("Jump of HydroStress Name"), dl->qp_scalar);
    jumpHydroStress= tjths;
    this->addEvaluatedField(jumpHydroStress);

    PHX::MDField<ScalarT,Cell,QuadPoint>
    tmpths(p.get<std::string>("MidPlane HydroStress Name"), dl->qp_scalar);
    midPlaneHydroStress= tmpths;
    this->addEvaluatedField(midPlaneHydroStress);
  }

  std::vector<PHX::DataLayout::size_type> dims;
  dl->node_vector->dimensions(dims);
  worksetSize = dims[0];
  numNodes = dims[1];
  numDims = dims[2];

  numQPs = cubature->getNumPoints();

  numPlaneNodes = numNodes / 2;
  numPlaneDims = numDims - 1;

#ifdef ALBANY_VERBOSE
    std::cout << "in Surface Scalar Jump" << std::endl;
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
  void SurfaceScalarJump<EvalT, Traits>::
  postRegistrationSetup(typename Traits::SetupData d,
                        PHX::FieldManager<Traits>& fm)
  {
  //  this->utils.setFieldData(scalar,fm);
 //   this->utils.setFieldData(scalarJump,fm);
 //   this->utils.setFieldData(scalarAverage,fm);

    if (haveTemperature){
    	this->utils.setFieldData(nodalTemperature,fm);
    	this->utils.setFieldData(midPlaneTemperature,fm);
    	this->utils.setFieldData(jumpTemperature,fm);
    }
    if (haveTransport){
    	this->utils.setFieldData(nodalTransport,fm);
    	this->utils.setFieldData(midPlaneTransport,fm);
    	this->utils.setFieldData(jumpTransport,fm);
    }
    if (haveHydroStress) {
    	 this->utils.setFieldData(nodalHydroStress,fm);
     	this->utils.setFieldData(midPlaneHydroStress,fm);
     	this->utils.setFieldData(jumpHydroStress,fm);
    };
    if (havePorePressure) {
    	 this->utils.setFieldData(nodalPorePressure,fm);
     	this->utils.setFieldData(midPlanePorePressure,fm);
     	this->utils.setFieldData(jumpPorePressure,fm);
    };

  }

  //**********************************************************************
  template<typename EvalT, typename Traits>
  void SurfaceScalarJump<EvalT, Traits>::
  evaluateFields(typename Traits::EvalData workset)
  {
    ScalarT scalarA(0.0), scalarB(0.0);
    /*
    for (int cell=0; cell < workset.numCells; ++cell) {
      for (int pt=0; pt < numQPs; ++pt) {
        scalarA = 0.0;
        scalarB = 0.0;
        for (int node=0; node < numPlaneNodes; ++node) {
          int topNode = node + numPlaneNodes;
          	  scalarA += refValues(node, pt) * scalar(cell, node);
          	  scalarB += refValues(node, pt) * scalar(cell, topNode);
        }
        scalarJump(cell,pt) = scalarB - scalarA;
        scalarAverage(cell,pt) = 0.5*(scalarB + scalarA);
      }
    }
    */
    if (havePorePressure) {
    	for (int cell=0; cell < workset.numCells; ++cell) {
    		for (int pt=0; pt < numQPs; ++pt) {
    			scalarA = 0.0;
    			scalarB = 0.0;
    			for (int node=0; node < numPlaneNodes; ++node) {
    				int topNode = node + numPlaneNodes;
    					scalarA += refValues(node, pt) * nodalPorePressure(cell, node);
    					scalarB += refValues(node, pt) * nodalPorePressure(cell, topNode);
        	}
        	jumpPorePressure(cell,pt) = scalarB - scalarA;
        	midPlanePorePressure(cell,pt) = 0.5*(scalarB + scalarA);
      	  }
    	}
    }
    if (haveTemperature) {
    	for (int cell=0; cell < workset.numCells; ++cell) {
    		for (int pt=0; pt < numQPs; ++pt) {
    			scalarA = 0.0;
    			scalarB = 0.0;
    			for (int node=0; node < numPlaneNodes; ++node) {
    				int topNode = node + numPlaneNodes;
    					scalarA += refValues(node, pt) * nodalTemperature(cell, node);
    					scalarB += refValues(node, pt) * nodalTemperature(cell, topNode);
        	}
        	jumpTemperature(cell,pt) = scalarB - scalarA;
        	midPlaneTemperature(cell,pt) = 0.5*(scalarB + scalarA);
      	  }
    	}
    }

    if (haveTransport) {
    	for (int cell=0; cell < workset.numCells; ++cell) {
    		for (int pt=0; pt < numQPs; ++pt) {
    			scalarA = 0.0;
    			scalarB = 0.0;
    			for (int node=0; node < numPlaneNodes; ++node) {
    				int topNode = node + numPlaneNodes;
    					scalarA += refValues(node, pt) * nodalTransport(cell, node);
    					scalarB += refValues(node, pt) * nodalTransport(cell, topNode);
        	}
        	jumpTransport(cell,pt) = scalarB - scalarA;
        	midPlaneTransport(cell,pt) = 0.5*(scalarB + scalarA);
      	  }
    	}
    }

    if (haveHydroStress) {
    	for (int cell=0; cell < workset.numCells; ++cell) {
    		for (int pt=0; pt < numQPs; ++pt) {
    			scalarA = 0.0;
    			scalarB = 0.0;
    			for (int node=0; node < numPlaneNodes; ++node) {
    				int topNode = node + numPlaneNodes;
    					scalarA += refValues(node, pt) * nodalHydroStress(cell, node);
    					scalarB += refValues(node, pt) * nodalHydroStress(cell, topNode);
        	}
        	jumpHydroStress(cell,pt) = scalarB - scalarA;
        	midPlaneHydroStress(cell,pt) = 0.5*(scalarB + scalarA);
      	  }
    	}
    }

  }

  //**********************************************************************
}

