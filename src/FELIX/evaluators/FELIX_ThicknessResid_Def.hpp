//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Teuchos_TestForException.hpp"
#include "Teuchos_VerboseObject.hpp"
#include "Phalanx_DataLayout.hpp"
#include "Phalanx_TypeStrings.hpp"
#include "Intrepid2_FunctionSpaceTools.hpp"

//uncomment the following line if you want debug output to be printed to screen
//#define OUTPUT_TO_SCREEN



namespace FELIX {

//**********************************************************************
template<typename EvalT, typename Traits>
ThicknessResid<EvalT, Traits>::
ThicknessResid(const Teuchos::ParameterList& p,
              const Teuchos::RCP<Albany::Layouts>& dl) :
  dH        (p.get<std::string> ("Thickness Increment Variable Name"), dl->node_scalar),
  H0       (p.get<std::string> ("Past Thickness Name"), dl->node_scalar),
  V        (p.get<std::string> ("Averaged Velocity Variable Name"), dl->node_vector),
  coordVec (p.get<std::string> ("Coordinate Vector Name"), dl->vertices_vector),
  Residual (p.get<std::string> ("Residual Name"), dl->node_scalar)
{

  dt = p.get<Teuchos::RCP<double> >("Time Step Ptr");

  if (p.isType<const std::string>("Mesh Part"))
    meshPart = p.get<const std::string>("Mesh Part");
  else
    meshPart = "upperside";

  if(p.isParameter("SMB Name")) {
   SMB_ptr = Teuchos::rcp(new PHX::MDField<ScalarT,Cell,Node>(p.get<std::string> ("SMB Name"), dl->node_scalar));
   have_SMB = true;
  } else
    have_SMB = false;

  Teuchos::RCP<const Albany::MeshSpecsStruct> meshSpecs = p.get<Teuchos::RCP<const Albany::MeshSpecsStruct> >("Mesh Specs Struct");

  this->addDependentField(dH);
  this->addDependentField(H0);
  this->addDependentField(V);
  this->addDependentField(coordVec);
  if(have_SMB)
    this->addDependentField(*SMB_ptr);

  this->addEvaluatedField(Residual);


  this->setName("ThicknessResid"+PHX::typeAsString<EvalT>());

  std::vector<PHX::DataLayout::size_type> dims;
  dl->node_vector->dimensions(dims);
  numNodes = dims[1];
  numVecFODims  = std::min(dims[2], PHX::DataLayout::size_type(2));

  dl->qp_gradient->dimensions(dims);
  cellDims = dims[2];

  const CellTopologyData * const elem_top = &meshSpecs->ctd;

  intrepidBasis = Albany::getIntrepid2Basis(*elem_top);

  cellType = Teuchos::rcp(new shards::CellTopology(elem_top));

  Intrepid2::DefaultCubatureFactory<RealType> cubFactory;
  cubatureDegree = p.isParameter("Cubature Degree") ? p.get<int>("Cubature Degree") : meshSpecs->cubatureDegree;
  numNodes = intrepidBasis->getCardinality();

  physPointsCell.resize(1, numNodes, cellDims);
  dofCell.resize(1, numNodes);
  dofCellVec.resize(1, numNodes, numVecFODims);

  Teuchos::RCP<Teuchos::FancyOStream> out(Teuchos::VerboseObjectBase::getDefaultOStream());
#ifdef OUTPUT_TO_SCREEN
*out << " in FELIX Thickness residual! " << std::endl;
*out << " numNodes = " << numNodes << std::endl; 
#endif
}

//**********************************************************************
template<typename EvalT, typename Traits>
void ThicknessResid<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(dH,fm);
  this->utils.setFieldData(H0,fm);
  this->utils.setFieldData(V,fm);
  this->utils.setFieldData(coordVec, fm);
  if(have_SMB)
    this->utils.setFieldData(*SMB_ptr, fm);

  this->utils.setFieldData(Residual,fm);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void ThicknessResid<EvalT, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  typedef Intrepid2::FunctionSpaceTools FST; 

  // Initialize residual to 0.0
  Kokkos::deep_copy(Residual.get_kokkos_view(), ScalarT(0.0));

  const Albany::SideSetList& ssList = *(workset.sideSets);
  Albany::SideSetList::const_iterator it = ssList.find(meshPart);


  if (it != ssList.end()) {
    const std::vector<Albany::SideStruct>& sideSet = it->second;

    Intrepid2::FieldContainer<ScalarT> dH_Side;
    Intrepid2::FieldContainer<ScalarT> SMB_Side;
    Intrepid2::FieldContainer<ScalarT> H0_Side;
    Intrepid2::FieldContainer<ScalarT> V_Side;

    // Loop over the sides that form the boundary condition
    for (std::size_t iSide = 0; iSide < sideSet.size(); ++iSide) { // loop over the sides on this ws and name

      // Get the data that corresponds to the side
      const int elem_GID = sideSet[iSide].elem_GID;
      const int elem_LID = sideSet[iSide].elem_LID;
      const int elem_side = sideSet[iSide].side_local_id;

      const CellTopologyData_Subcell& side =  cellType->getCellTopologyData()->side[elem_side];
      sideType = Teuchos::rcp(new shards::CellTopology(side.topology));
      int numSideNodes = sideType->getNodeCount();
      Intrepid2::DefaultCubatureFactory<RealType> cubFactory;
      cubatureSide = cubFactory.create(*sideType, cubatureDegree);
      sideDims = sideType->getDimension();
      numQPsSide = cubatureSide->getNumPoints();

      // Allocate Temporary FieldContainers
      cubPointsSide.resize(numQPsSide, sideDims);
      refPointsSide.resize(numQPsSide, cellDims);
      cubWeightsSide.resize(numQPsSide);
      physPointsSide.resize(1, numQPsSide, cellDims);
      dofSide.resize(1, numQPsSide);
      dofSideVec.resize(1, numQPsSide, numVecFODims);

      // Do the BC one side at a time for now
      jacobianSide.resize(1, numQPsSide, cellDims, cellDims);
      invJacobianSide.resize(1, numQPsSide, cellDims, cellDims);
      jacobianSide_det.resize(1, numQPsSide);

      weighted_measure.resize(1, numQPsSide);
      basis_refPointsSide.resize(numNodes, numQPsSide);
      basisGrad_refPointsSide.resize(numNodes, numQPsSide, cellDims);
      trans_basis_refPointsSide.resize(1, numNodes, numQPsSide);
      trans_gradBasis_refPointsSide.resize(1, numNodes, numQPsSide, cellDims);
      weighted_trans_basis_refPointsSide.resize(1, numNodes, numQPsSide);

      // Pre-Calculate reference element quantitites
      cubatureSide->getCubature(cubPointsSide, cubWeightsSide);

      dH_Side.resize(numQPsSide);
      SMB_Side.resize(numQPsSide);
      H0_Side.resize(numQPsSide);
      V_Side.resize(numQPsSide, numVecFODims);

      // Copy the coordinate data over to a temp container
     for (std::size_t node = 0; node < numNodes; ++node) {
       for (std::size_t dim = 0; dim < cellDims; ++dim)
         physPointsCell(0, node, dim) = coordVec(elem_LID, node, dim);
       physPointsCell(0, node, cellDims-1) = -1.0; //set z=-1 on internal cell nodes and z=0 side (see next lines).
     }
     for (int i = 0; i < numSideNodes; ++i)
       physPointsCell(0, side.node[i], cellDims-1) = 0.0;  //set z=0 on side

      // Map side cubature points to the reference parent cell based on the appropriate side (elem_side)
      Intrepid2::CellTools<RealType>::mapToReferenceSubcell(refPointsSide, cubPointsSide, sideDims, elem_side, *cellType);

      // Calculate side geometry
      Intrepid2::CellTools<MeshScalarT>::setJacobian(jacobianSide, refPointsSide, physPointsCell, *cellType);

      Intrepid2::CellTools<MeshScalarT>::setJacobianInv(invJacobianSide, jacobianSide);

      Intrepid2::CellTools<MeshScalarT>::setJacobianDet(jacobianSide_det, jacobianSide);

      if (sideDims < 2) { //for 1 and 2D, get weighted edge measure
        Intrepid2::FunctionSpaceTools::computeEdgeMeasure<MeshScalarT>(weighted_measure, jacobianSide, cubWeightsSide, elem_side, *cellType);
      } else { //for 3D, get weighted face measure
        Intrepid2::FunctionSpaceTools::computeFaceMeasure<MeshScalarT>(weighted_measure, jacobianSide, cubWeightsSide, elem_side, *cellType);
      }

      // Values of the basis functions at side cubature points, in the reference parent cell domain
      intrepidBasis->getValues(basis_refPointsSide, refPointsSide, Intrepid2::OPERATOR_VALUE);

      intrepidBasis->getValues(basisGrad_refPointsSide, refPointsSide, Intrepid2::OPERATOR_GRAD);

      // Transform values of the basis functions
      Intrepid2::FunctionSpaceTools::HGRADtransformVALUE<MeshScalarT>(trans_basis_refPointsSide, basis_refPointsSide);

      Intrepid2::FunctionSpaceTools::HGRADtransformGRAD<MeshScalarT>(trans_gradBasis_refPointsSide, invJacobianSide, basisGrad_refPointsSide);

      // Multiply with weighted measure
      Intrepid2::FunctionSpaceTools::multiplyMeasure<MeshScalarT>(weighted_trans_basis_refPointsSide, weighted_measure, trans_basis_refPointsSide);

      // Map cell (reference) cubature points to the appropriate side (elem_side) in physical space
      Intrepid2::CellTools<MeshScalarT>::mapToPhysicalFrame(physPointsSide, refPointsSide, physPointsCell, intrepidBasis);

      // Map cell (reference) degree of freedom points to the appropriate side (elem_side)
      Intrepid2::FieldContainer<ScalarT> dH_Cell(numNodes);
      Intrepid2::FieldContainer<ScalarT> SMB_Cell(numNodes);
      Intrepid2::FieldContainer<ScalarT> H0_Cell(numNodes);
      Intrepid2::FieldContainer<ScalarT> V_Cell(numNodes, numVecFODims);
      Intrepid2::FieldContainer<ScalarT> gradH_Side(numQPsSide, numVecFODims);
      Intrepid2::FieldContainer<ScalarT> divV_Side(numQPsSide);

      gradH_Side.initialize();
      divV_Side.initialize();

      std::map<LO, std::size_t>::const_iterator it;


      for (int i = 0; i < numSideNodes; ++i){
        std::size_t node = side.node[i]; //it->second;
        dH_Cell(node) = dH(elem_LID, node);
        H0_Cell(node) = H0(elem_LID, node);
        SMB_Cell(node) = have_SMB ? (*SMB_ptr)(elem_LID, node) : ScalarT(0.0);
        for (std::size_t dim = 0; dim < numVecFODims; ++dim)
          V_Cell(node, dim) = V(elem_LID, node, dim);
      }

      // This is needed, since evaluate currently sums into
      for (int qp = 0; qp < numQPsSide; qp++) {
        dH_Side(qp) = 0.0;
        H0_Side(qp) = 0.0;
        SMB_Side(qp) = 0.0;
        for (std::size_t dim = 0; dim < numVecFODims; ++dim)
          V_Side(qp, dim) = 0.0;
      }

      // Get dof at cubature points of appropriate side (see DOFVecInterpolation evaluator)
      for (int i = 0; i < numSideNodes; ++i){
        std::size_t node = side.node[i]; //it->second;
        for (std::size_t qp = 0; qp < numQPsSide; ++qp) {
          const MeshScalarT& tmp = trans_basis_refPointsSide(0, node, qp);
          dH_Side(qp) += dH_Cell(node) * tmp;
          SMB_Side(qp) += SMB_Cell(node) * tmp;
          H0_Side(qp) += H0_Cell(node) * tmp;
          for (std::size_t dim = 0; dim < numVecFODims; ++dim)
            V_Side(qp, dim) += V_Cell(node, dim) * tmp;
        }
      }

      for (std::size_t qp = 0; qp < numQPsSide; ++qp) {
        for (int i = 0; i < numSideNodes; ++i){
          std::size_t node = side.node[i]; //it->second;
          for (std::size_t dim = 0; dim < numVecFODims; ++dim) {
            const MeshScalarT& tmp = trans_gradBasis_refPointsSide(0, node, qp, dim);
            gradH_Side(qp, dim) += H0_Cell(node) * tmp;
            divV_Side(qp) += V_Cell(node, dim) * tmp;
          }
        }
      }

      for (int i = 0; i < numSideNodes; ++i){
        std::size_t node = side.node[i]; //it->second;
        ScalarT res = 0;
        for (std::size_t qp = 0; qp < numQPsSide; ++qp) {
          ScalarT divHV = divV_Side(qp)* H0_Side(qp);
          for (std::size_t dim = 0; dim < numVecFODims; ++dim)
            divHV += gradH_Side(qp, dim)*V_Side(qp,dim);

         ScalarT tmp = dH_Side(qp) + (*dt/1000.0) * divHV - *dt*SMB_Side(qp);
          res +=tmp * weighted_trans_basis_refPointsSide(0, node, qp);
        }
        Residual(elem_LID,node) = res;
      }
    }
  }
}

//**********************************************************************
}

