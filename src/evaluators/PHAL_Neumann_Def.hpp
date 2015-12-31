//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

//IK, 9/13/14: only Epetra is SG and MP

#include "Teuchos_TestForException.hpp"
#include "Phalanx_DataLayout.hpp"
#include <string>

#include "Intrepid2_FunctionSpaceTools.hpp"
#include "Sacado_ParameterRegistration.hpp"

//uncomment the following line if you want debug output to be printed to screen
//#define OUTPUT_TO_SCREEN


namespace PHAL {
const double pi = 3.1415926535897932385;

//**********************************************************************
template<typename EvalT, typename Traits>
NeumannBase<EvalT, Traits>::
NeumannBase(const Teuchos::ParameterList& p) :

  dl             (p.get<Teuchos::RCP<Albany::Layouts> >("Layouts Struct")),
  meshSpecs      (p.get<Teuchos::RCP<Albany::MeshSpecsStruct> >("Mesh Specs Struct")),
  offset         (p.get<Teuchos::Array<int> >("Equation Offset")),
  sideSetID      (p.get<std::string>("Side Set ID")),
  coordVec       (p.get<std::string>("Coordinate Vector Name"), dl->vertices_vector)
{
  // the input.xml string "NBC on SS sidelist_12 for DOF T set dudn" (or something like it)
  name = p.get< std::string >("Neumann Input String");

  // The input.xml argument for the above string
  inputValues = p.get<Teuchos::Array<double> >("Neumann Input Value");

  // The input.xml argument for the above string
  inputConditions = p.get< std::string >("Neumann Input Conditions");
 
  // The DOF offsets are contained in the Equation Offset array. The length of this array are the
  // number of DOFs we will set each call
  numDOFsSet = offset.size();

  // Set up values as parameters for parameter library
  Teuchos::RCP<ParamLib> paramLib = p.get< Teuchos::RCP<ParamLib> > ("Parameter Library");

  // If we are doing a Neumann internal boundary with a "scaled jump",
  // build a scale lookup table from the materialDB file (this must exist)

  int position;

  if((inputConditions == "scaled jump" || inputConditions == "robin") &&
     p.isType<Teuchos::RCP<QCAD::MaterialDatabase> >("MaterialDB")){

    //! Material database - holds the scaling we need
    Teuchos::RCP<QCAD::MaterialDatabase> materialDB =
      p.get< Teuchos::RCP<QCAD::MaterialDatabase> >("MaterialDB");

     // User has specified conditions on sideset normal
    if(inputConditions == "scaled jump") {
      bc_type = INTJUMP;
      const_val = inputValues[0];
      this->registerSacadoParameter(name, paramLib);
    }
    else { // inputConditions == "robin"
      bc_type = ROBIN;
      robin_vals[0] = inputValues[0]; // dof_value
      robin_vals[1] = inputValues[1]; // coeff multiplying difference (dof - dof_value) -- could be permittivity/distance (distance in mesh units)
      robin_vals[2] = inputValues[2]; // jump in slope (like plain Neumann bc)

      for(int i = 0; i < 3; i++) {
        std::stringstream ss; ss << name << "[" << i << "]";
        this->registerSacadoParameter(ss.str(), paramLib);
      }
    }

     // Build a vector to hold the scaling from the material DB
     matScaling.resize(meshSpecs->ebNameToIndex.size());

     // iterator over all ebnames in the mesh

     std::map<std::string, int>::const_iterator it;
     for(it = meshSpecs->ebNameToIndex.begin(); it != meshSpecs->ebNameToIndex.end(); it++){

//      std::cout << "Searching for a value for \"Flux Scale\" in material database for element block: "
//        << it->first << std::endl;

       TEUCHOS_TEST_FOR_EXCEPTION(!materialDB->isElementBlockParam(it->first, "Flux Scale"),
         Teuchos::Exceptions::InvalidParameter, "Cannot locate the value of \"Flux Scale\" for element block "
                                  << it->first << " in the material database");

       matScaling[it->second] =
         materialDB->getElementBlockParam<double>(it->first, "Flux Scale");

     }


     // In the robin boundary condition case, the NBC depends on the solution (dof) field
     if (inputConditions == "robin") {
      // Currently, the Neumann evaluator doesn't handle the case when the degree of freedom is a vector.
      // It wouldn't be difficult to have the boundary condition use a component of the vector, but I'm
      // not sure this is the correct behavior.  In any case, the only time when this evaluator needs
      // a degree of freedom value is in the "robin" case.
      TEUCHOS_TEST_FOR_EXCEPTION(p.get<bool>("Vector Field") == true,
                             Teuchos::Exceptions::InvalidParameter,
                             std::endl << "Error: \"Robin\" Neumann boundary conditions "
                             << "only supported when the DOF is not a vector" << std::endl);

       PHX::MDField<ScalarT,Cell,Node> tmp(p.get<std::string>("DOF Name"),
           p.get<Teuchos::RCP<PHX::DataLayout> >("DOF Data Layout"));
       dof = tmp;

       this->addDependentField(dof);
     }
  }

  // else parse the input to determine what type of BC to calculate

    // is there a "(" in the string?
  else if((position = inputConditions.find_first_of("(")) != std::string::npos){

      if(inputConditions.find("t_x", position + 1)){
        // User has specified conditions in base coords
        bc_type = TRACTION;
      }
      else {
        // User has specified conditions in base coords
        bc_type = COORD;
      }

      dudx.resize(meshSpecs->numDim);
      for(int i = 0; i < dudx.size(); i++)
        dudx[i] = inputValues[i];

      for(int i = 0; i < dudx.size(); i++) {
        std::stringstream ss; ss << name << "[" << i << "]";
        this->registerSacadoParameter(ss.str(), paramLib);
      }
  }
  else if(inputConditions == "P"){ // Pressure boundary condition for Elasticity

      // User has specified a pressure condition
      bc_type = PRESS;
      const_val = inputValues[0];
      this->registerSacadoParameter(name, paramLib);

  }
  else if(inputConditions == "basal"){ // Basal boundary condition for FELIX
      rho = p.get<double>("Ice Density");
      rho_w = p.get<double>("Water Density");
      stereographicMapList = p.get<Teuchos::ParameterList*>("Stereographic Map");
      useStereographicMap = stereographicMapList->get("Use Stereographic Map", false);
      if(useStereographicMap)
        this->addDependentField(coordVec);
      // User has specified alpha and beta to set BC d(flux)/dn = beta*u + alpha or d(flux)/dn = (alpha + beta1*x + beta2*y + beta3*sqrt(x*x+y*y))*u
      bc_type = BASAL;
      int numInputs = inputValues.size(); //number of arguments user entered at command line.

      TEUCHOS_TEST_FOR_EXCEPTION(numInputs > 5,
                             Teuchos::Exceptions::InvalidParameter,
                             std::endl << "Error in basal boundary condition: you have entered an Array(double) of size " << numInputs <<
                             " (" << numInputs << " inputs) in your input file, but the boundary condition supports a maximum of 5 inputs." << std::endl);

      for (int i = 0; i < numInputs; i++)
        robin_vals[i] = inputValues[i]; //0 = beta, 1 = alpha, 2 = beta1, 3 = beta2, 4 = beta3

      for (int i = numInputs; i < 5; i++) //if user gives less than 5 inputs in the input file, set the remaining robin_vals entries to 0
        robin_vals[i] = 0.0;

      for(int i = 0; i < 5; i++) {
        std::stringstream ss; ss << name << "[" << i << "]";
        this->registerSacadoParameter(ss.str(), paramLib);
      }
       PHX::MDField<ScalarT,Cell,Node,VecDim> tmp(p.get<std::string>("DOF Name"),
           p.get<Teuchos::RCP<PHX::DataLayout> >("DOF Data Layout"));
       dofVec = tmp;
#ifdef ALBANY_FELIX
      beta_field = PHX::MDField<ScalarT,Cell,Node>(
        p.get<std::string>("Beta Field Name"), dl->node_scalar);
      thickness_field = PHX::MDField<ScalarT,Cell,Node>(
        p.get<std::string>("thickness Field Name"), dl->node_scalar);
      bedTopo_field = PHX::MDField<ScalarT,Cell,Node>(
        p.get<std::string>("BedTopo Field Name"), dl->node_scalar);
#endif

      betaName = p.get<std::string>("BetaXY");
      L = p.get<double>("L");
#ifdef OUTPUT_TO_SCREEN
      *out << "BetaName: " << betaName << std::endl;
      *out << "L: " << L << std::endl;
#endif
      if (betaName == "Constant")
        beta_type = CONSTANT;
      else if (betaName == "ExpTrig")
        beta_type = EXPTRIG;
      else if (betaName == "ISMIP-HOM Test C")
        beta_type = ISMIP_HOM_TEST_C;
      else if (betaName == "ISMIP-HOM Test D")
        beta_type = ISMIP_HOM_TEST_D;
      else if (betaName == "Confined Shelf")
        beta_type = CONFINEDSHELF;
      else if (betaName == "Circular Shelf")
        beta_type = CIRCULARSHELF;
      else if (betaName == "Dome UQ")
        beta_type = DOMEUQ;
      else if (betaName == "Scalar Field")
        beta_type = SCALAR_FIELD;
      else if (betaName == "Exponent Of Scalar Field")
        beta_type = EXP_SCALAR_FIELD;
      else if (betaName == "Power Law Scalar Field")
        beta_type = POWERLAW_SCALAR_FIELD;
      else if (betaName == "GLP Scalar Field")
        beta_type = GLP_SCALAR_FIELD;
      else if (betaName == "Exponent Of Scalar Field Times Thickness")
        beta_type = EXP_SCALAR_FIELD_THK;
      else if (betaName == "FELIX XZ MMS") 
        beta_type = FELIX_XZ_MMS; 
      else TEUCHOS_TEST_FOR_EXCEPTION(true,Teuchos::Exceptions::InvalidParameter,
        std::endl << "The BetaXY name: \"" << betaName << "\" is not a valid name" << std::endl);

      this->addDependentField(dofVec);
#ifdef ALBANY_FELIX
      this->addDependentField(beta_field);
      this->addDependentField(thickness_field);
      this->addDependentField(bedTopo_field);
#endif
  }
  else if(inputConditions == "basal_scalar_field"){ // Basal boundary condition for FELIX, where the basal sliding coefficient is a scalar field
      stereographicMapList = p.get<Teuchos::ParameterList*>("Stereographic Map");
      useStereographicMap = stereographicMapList->get("Use Stereographic Map", false);

      if(useStereographicMap)
        this->addDependentField(coordVec);

      // User has specified scale to set BC d(flux)/dn = scale*beta*u, where beta is a scalar field
      bc_type = BASAL_SCALAR_FIELD;
      robin_vals[0] = inputValues[0]; // scale

      for(int i = 0; i < 1; i++) {
        std::stringstream ss; ss << name << "[" << i << "]";
        this->registerSacadoParameter(ss.str(), paramLib);
      }
       PHX::MDField<ScalarT,Cell,Node,VecDim> tmp(p.get<std::string>("DOF Name"),
           p.get<Teuchos::RCP<PHX::DataLayout> >("DOF Data Layout"));
       dofVec = tmp;
#ifdef ALBANY_FELIX
      beta_field = PHX::MDField<ScalarT,Cell,Node>(
                    p.get<std::string>("Beta Field Name"), dl->node_scalar);
      this->addDependentField(beta_field);
#endif
      this->addDependentField(dofVec);
  }
  else if(inputConditions == "lateral"){ // Basal boundary condition for FELIX
       stereographicMapList = p.get<Teuchos::ParameterList*>("Stereographic Map");
       useStereographicMap = stereographicMapList->get("Use Stereographic Map", false);
       if(useStereographicMap)
         this->addDependentField(coordVec);
        // User has specified alpha and beta to set BC d(flux)/dn = beta*u + alpha or d(flux)/dn = (alpha + beta1*x + beta2*y + beta3*sqrt(x*x+y*y))*u
        bc_type = LATERAL;
        beta_type = LATERAL_BACKPRESSURE;

        g = p.get<double>("Gravity");
        rho = p.get<double>("Ice Density");
        rho_w = p.get<double>("Water Density");

#ifdef OUTPUT_TO_SCREEN
        std::cout << "g, rho, rho_w: " << g << ", " << rho << ", " << rho_w << std::endl;
#endif
      robin_vals[0] = inputValues[0]; // immersed ratio

      int numInputs = inputValues.size(); //number of arguments user entered at command line.

      //The following is for backward compatibility: the lateral BC used to have 5 inputs, now really it has 1. 
      for (int i = numInputs; i < 5; i++) 
        robin_vals[i] = 0.0;
        
      //The following should really go to 1 but above backward compatibility line keeps this at length 5.
      for(int i = 0; i < 5; i++) {
        std::stringstream ss; ss << name << "[" << i << "]";
        this->registerSacadoParameter(ss.str(), paramLib);
      }
       PHX::MDField<ScalarT,Cell,Node,VecDim> tmp(p.get<std::string>("DOF Name"),
             p.get<Teuchos::RCP<PHX::DataLayout> >("DOF Data Layout"));
       dofVec = tmp;
#ifdef ALBANY_FELIX
       thickness_field = PHX::MDField<ScalarT,Cell,Node>(
                           p.get<std::string>("thickness Field Name"), dl->node_scalar);
       elevation_field = PHX::MDField<ScalarT,Cell,Node>(
                           p.get<std::string>("Elevation Field Name"), dl->node_scalar);

        this->addDependentField(thickness_field);
        this->addDependentField(elevation_field);
#endif

        this->addDependentField(dofVec);
    }

  else {

      // User has specified conditions on sideset normal
      bc_type = NORMAL;
      const_val = inputValues[0];
      this->registerSacadoParameter(name, paramLib);

  }

  this->addDependentField(coordVec);

  PHX::Tag<ScalarT> fieldTag(name, dl->dummy);

  this->addEvaluatedField(fieldTag);

  // Build element and side integration support

  const CellTopologyData * const elem_top = &meshSpecs->ctd;

  intrepidBasis = Albany::getIntrepid2Basis(*elem_top);

  cellType = Teuchos::rcp(new shards::CellTopology (elem_top));

  Intrepid2::DefaultCubatureFactory<RealType> cubFactory;
  cubatureCell = cubFactory.create(*cellType, meshSpecs->cubatureDegree);

  int cubatureDegree = (p.get<int>("Cubature Degree") > 0 ) ? p.get<int>("Cubature Degree") : meshSpecs->cubatureDegree;

  int numSidesOnElem = elem_top->side_count;
  sideType.resize(numSidesOnElem);
  cubatureSide.resize(numSidesOnElem);
  side_type.resize(numSidesOnElem);

  // Build containers that depend on side topology
  int maxSideDim(0), maxNumQpSide(0);
  const char* sideTypeName;

  for(int i=0; i<numSidesOnElem; ++i) {
    sideType[i] = Teuchos::rcp(new shards::CellTopology(elem_top->side[i].topology));
    cubatureSide[i] = cubFactory.create(*sideType[i], cubatureDegree);
    maxSideDim = std::max( maxSideDim, (int)sideType[i]->getDimension());
    maxNumQpSide = std::max(maxNumQpSide, (int)cubatureSide[i]->getNumPoints());
    sideTypeName = sideType[i]->getName();
    if(strncasecmp(sideTypeName, "Line", 4) == 0)
      side_type[i] = LINE;
    else if(strncasecmp(sideTypeName, "Tri", 3) == 0)
      side_type[i] = TRI;
    else if(strncasecmp(sideTypeName, "Quad", 4) == 0)
      side_type[i] = QUAD;
    else
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
        "PHAL_Neumann: side type : " << sideTypeName << " is not supported." << std::endl);
  }

  numNodes = intrepidBasis->getCardinality();

  // Get Dimensions
  std::vector<PHX::DataLayout::size_type> dim;
  dl->qp_tensor->dimensions(dim);
  int containerSize = dim[0];
  numQPs = dim[1];
  cellDims = dim[2];

  // Allocate Temporary FieldContainers
  physPointsCell.resize(1, numNodes, cellDims);
  dofCell.resize(1, numNodes);
  dofCellVec.resize(1, numNodes, numDOFsSet);
  neumann.resize(containerSize, numNodes, numDOFsSet);

  // Allocate Temporary FieldContainers based on max sizes of sides. Need to be resized later for each side.
  cubPointsSide.resize(maxNumQpSide, maxSideDim);
  refPointsSide.resize(maxNumQpSide, cellDims);
  cubWeightsSide.resize(maxNumQpSide);
  physPointsSide.resize(1, maxNumQpSide, cellDims);
  dofSide.resize(1, maxNumQpSide);
  dofSideVec.resize(1, maxNumQpSide, numDOFsSet);

  // Do the BC one side at a time for now
  jacobianSide.resize(1, maxNumQpSide, cellDims, cellDims);
  jacobianSide_det.resize(1, maxNumQpSide);

  weighted_measure.resize(1, maxNumQpSide);
  basis_refPointsSide.resize(numNodes, maxNumQpSide);
  trans_basis_refPointsSide.resize(1, numNodes, maxNumQpSide);
  weighted_trans_basis_refPointsSide.resize(1, numNodes, maxNumQpSide);

  data.resize(1, maxNumQpSide, numDOFsSet);


  this->setName(name);
}

//**********************************************************************
template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
postRegistrationSetup(typename Traits::SetupData d,
                      PHX::FieldManager<Traits>& fm)
{
  this->utils.setFieldData(coordVec,fm);
  if (inputConditions == "robin") this->utils.setFieldData(dof,fm);
#ifdef ALBANY_FELIX
  else if (inputConditions == "basal" || inputConditions == "basal_scalar_field")
  {
    this->utils.setFieldData(dofVec,fm);
    this->utils.setFieldData(beta_field,fm);
    if (inputConditions == "basal") {
      this->utils.setFieldData(thickness_field,fm);
      this->utils.setFieldData(bedTopo_field,fm);
    }
  }
  else if(inputConditions == "lateral")
  {
    this->utils.setFieldData(dofVec,fm);
    this->utils.setFieldData(thickness_field,fm);
    this->utils.setFieldData(elevation_field,fm);
  }
#endif
  // Note, we do not need to add dependent field to fm here for output - that is done
  // by Neumann Aggregator
}

template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
evaluateNeumannContribution(typename Traits::EvalData workset)
{

  // setJacobian only needs to be RealType since the data type is only
  //  used internally for Basis Fns on reference elements, which are
  //  not functions of coordinates. This save 18min of compile time!!!

  // GAH: Note that this loosely follows from
  // $TRILINOS_DIR/packages/intrepid/test/Discretization/Basis/HGRAD_QUAD_C1_FEM/test_02.cpp

  if(workset.sideSets == Teuchos::null || this->sideSetID.length() == 0)

    TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
         "Side sets defined in input file but not properly specified on the mesh" << std::endl);


  const Albany::SideSetList& ssList = *(workset.sideSets);
  Albany::SideSetList::const_iterator it = ssList.find(this->sideSetID);

  for (std::size_t cell=0; cell < workset.numCells; ++cell)
   for (std::size_t node=0; node < numNodes; ++node)
     for (std::size_t dim=0; dim < numDOFsSet; ++dim)
             neumann(cell, node, dim) = 0.0;

  if(it == ssList.end()) return; // This sideset does not exist in this workset (GAH - this can go away
                                  // once we move logic to BCUtils

  Intrepid2::FieldContainer<ScalarT> betaOnSide;
  Intrepid2::FieldContainer<ScalarT> thicknessOnSide;
  Intrepid2::FieldContainer<ScalarT> bedTopoOnSide;
  Intrepid2::FieldContainer<ScalarT> elevationOnSide;

  const std::vector<Albany::SideStruct>& sideSet = it->second;

  // Loop over the sides that form the boundary condition

  for (std::size_t side=0; side < sideSet.size(); ++side) { // loop over the sides on this ws and name

    // Get the data that corresponds to the side

    const int elem_GID = sideSet[side].elem_GID;
    const int elem_LID = sideSet[side].elem_LID;
    const int elem_side = sideSet[side].side_local_id;

    int sideDims = sideType[elem_side]->getDimension();
    int numQPsSide = cubatureSide[elem_side]->getNumPoints();


    //need to resize containers because they depend on side topology
    cubPointsSide.resize(numQPsSide, sideDims);
    refPointsSide.resize(numQPsSide, cellDims);
    cubWeightsSide.resize(numQPsSide);
    physPointsSide.resize(1, numQPsSide, cellDims);
    dofSide.resize(1, numQPsSide);
    dofSideVec.resize(1, numQPsSide, numDOFsSet);

    // Do the BC one side at a time for now
    jacobianSide.resize(1, numQPsSide, cellDims, cellDims);
    jacobianSide_det.resize(1, numQPsSide);

    weighted_measure.resize(1, numQPsSide);
    basis_refPointsSide.resize(numNodes, numQPsSide);
    trans_basis_refPointsSide.resize(1, numNodes, numQPsSide);
    weighted_trans_basis_refPointsSide.resize(1, numNodes, numQPsSide);
    data.resize(1, numQPsSide, numDOFsSet);

    betaOnSide.resize(1,numQPsSide);
    thicknessOnSide.resize(1,numQPsSide);
    bedTopoOnSide.resize(1,numQPsSide);
    elevationOnSide.resize(1,numQPsSide);

    cubatureSide[elem_side]->getCubature(cubPointsSide, cubWeightsSide);

    // Copy the coordinate data over to a temp container

    for (std::size_t node=0; node < numNodes; ++node)
      for (std::size_t dim=0; dim < cellDims; ++dim)
        physPointsCell(0, node, dim) = coordVec(elem_LID, node, dim);


    // Map side cubature points to the reference parent cell based on the appropriate side (elem_side)
    Intrepid2::CellTools<RealType>::mapToReferenceSubcell
      (refPointsSide, cubPointsSide, sideDims, elem_side, *cellType);

    // Calculate side geometry
    Intrepid2::CellTools<MeshScalarT>::setJacobian
       (jacobianSide, refPointsSide, physPointsCell, *cellType);

    Intrepid2::CellTools<MeshScalarT>::setJacobianDet(jacobianSide_det, jacobianSide);

    if (sideDims < 2) { //for 1 and 2D, get weighted edge measure
      Intrepid2::FunctionSpaceTools::computeEdgeMeasure<MeshScalarT>
        (weighted_measure, jacobianSide, cubWeightsSide, elem_side, *cellType);
    }
    else { //for 3D, get weighted face measure
      Intrepid2::FunctionSpaceTools::computeFaceMeasure<MeshScalarT>
        (weighted_measure, jacobianSide, cubWeightsSide, elem_side, *cellType);
    }

    // Values of the basis functions at side cubature points, in the reference parent cell domain
    intrepidBasis->getValues(basis_refPointsSide, refPointsSide, Intrepid2::OPERATOR_VALUE);

    // Transform values of the basis functions
    Intrepid2::FunctionSpaceTools::HGRADtransformVALUE<MeshScalarT>
      (trans_basis_refPointsSide, basis_refPointsSide);

    // Multiply with weighted measure
    Intrepid2::FunctionSpaceTools::multiplyMeasure<MeshScalarT>
      (weighted_trans_basis_refPointsSide, weighted_measure, trans_basis_refPointsSide);

    // Map cell (reference) cubature points to the appropriate side (elem_side) in physical space
    Intrepid2::CellTools<MeshScalarT>::mapToPhysicalFrame
      (physPointsSide, refPointsSide, physPointsCell, intrepidBasis);

    // Map cell (reference) degree of freedom points to the appropriate side (elem_side)
    if(bc_type == ROBIN) {
      for (std::size_t node=0; node < numNodes; ++node)
        dofCell(0, node) = dof(elem_LID, node);

      // This is needed, since evaluate currently sums into
      for (int i=0; i < numQPsSide ; i++) dofSide(0,i) = 0.0;

      // Get dof at cubature points of appropriate side (see DOFInterpolation evaluator)
      Intrepid2::FunctionSpaceTools::
        evaluate<ScalarT>(dofSide, dofCell, trans_basis_refPointsSide);
    }

    // Map cell (reference) degree of freedom points to the appropriate side (elem_side)
    else if(bc_type == BASAL || bc_type == BASAL_SCALAR_FIELD) {
      Intrepid2::FieldContainer<ScalarT> betaOnCell(1, numNodes);
      Intrepid2::FieldContainer<ScalarT> thicknessOnCell(1, numNodes);
      Intrepid2::FieldContainer<ScalarT> bedTopoOnCell(1, numNodes);
      for (std::size_t node=0; node < numNodes; ++node)
      {
        betaOnCell(0,node) = beta_field(elem_LID,node);
#ifdef ALBANY_FELIX
        if(bc_type == BASAL) {
          thicknessOnCell(0,node) = thickness_field(elem_LID,node);
          bedTopoOnCell(0,node) = bedTopo_field(elem_LID,node);
        }
#endif
        for(int dim = 0; dim < numDOFsSet; dim++)
              dofCellVec(0,node,dim) = dofVec(elem_LID,node,this->offset[dim]);
      }

      // This is needed, since evaluate currently sums into
      for (int i=0; i < numQPsSide ; i++) betaOnSide(0,i) = 0.0;
      for (int i=0; i < numQPsSide ; i++) thicknessOnSide(0,i) = 0.0;
      for (int i=0; i < numQPsSide ; i++) bedTopoOnSide(0,i) = 0.0;
      for (int i=0; i < dofSideVec.size() ; i++) dofSideVec[i] = 0.0;

      // Get dof at cubature points of appropriate side (see DOFVecInterpolation evaluator)
      for (std::size_t node=0; node < numNodes; ++node) {
         for (std::size_t qp=0; qp < numQPsSide; ++qp) {
                 betaOnSide(0, qp)  += betaOnCell(0, node) * trans_basis_refPointsSide(0, node, qp);
                 thicknessOnSide(0, qp)  += thicknessOnCell(0, node) * trans_basis_refPointsSide(0, node, qp);
                 bedTopoOnSide(0, qp)  += bedTopoOnCell(0, node) * trans_basis_refPointsSide(0, node, qp);
            for (int dim = 0; dim < numDOFsSet; dim++) {
               dofSideVec(0, qp, dim)  += dofCellVec(0, node, dim) * trans_basis_refPointsSide(0, node, qp);
            }
          }
       }

      // Get dof at cubature points of appropriate side (see DOFVecInterpolation evaluator)
      //Intrepid2::FunctionSpaceTools::
        //evaluate<ScalarT>(dofSide, dofCell, trans_basis_refPointsSide);
    }
#ifdef ALBANY_FELIX
    else if(bc_type == LATERAL) {
          Intrepid2::FieldContainer<ScalarT> thicknessOnCell(1, numNodes);
          Intrepid2::FieldContainer<ScalarT> elevationOnCell(1, numNodes);
          for (std::size_t node=0; node < numNodes; ++node)
          {
                thicknessOnCell(0,node) = thickness_field(elem_LID,node);
                elevationOnCell(0,node) = elevation_field(elem_LID,node);
                for(int dim = 0; dim < numDOFsSet; dim++)
                  dofCellVec(0,node,dim) = dofVec(elem_LID,node,this->offset[dim]);
          }

          // This is needed, since evaluate currently sums into
          for (int i=0; i < numQPsSide ; i++)
          {
                  thicknessOnSide(0,i) = 0.0;
                  elevationOnSide(0,i) = 0.0;
          }
          for (int i=0; i < dofSideVec.size() ; i++) dofSideVec[i] = 0.0;

          // Get dof at cubature points of appropriate side (see DOFVecInterpolation evaluator)
          for (std::size_t node=0; node < numNodes; ++node) {
                 for (std::size_t qp=0; qp < numQPsSide; ++qp) {
                         thicknessOnSide(0, qp)  += thicknessOnCell(0, node) * trans_basis_refPointsSide(0, node, qp);
                         elevationOnSide(0, qp)  += elevationOnCell(0, node) * trans_basis_refPointsSide(0, node, qp);
                        for (int dim = 0; dim < numDOFsSet; dim++) {
                           dofSideVec(0, qp, dim)  += dofCellVec(0, node, dim) * trans_basis_refPointsSide(0, node, qp);
                        }
                  }
           }

          // Get dof at cubature points of appropriate side (see DOFVecInterpolation evaluator)
          //Intrepid2::FunctionSpaceTools::
        //evaluate<ScalarT>(dofSide, dofCell, trans_basis_refPointsSide);
    }
#endif
  // Transform the given BC data to the physical space QPs in each side (elem_side)

    switch(bc_type){

      case INTJUMP:
       {
         const ScalarT elem_scale = matScaling[sideSet[side].elem_ebIndex];
         calc_dudn_const(data, physPointsSide, jacobianSide, *cellType, cellDims, elem_side, elem_scale);
         break;
       }

      case ROBIN:
       {
         const ScalarT elem_scale = matScaling[sideSet[side].elem_ebIndex];
         calc_dudn_robin(data, physPointsSide, dofSide, jacobianSide, *cellType, cellDims, elem_side, elem_scale, robin_vals);
         break;
       }

      case NORMAL:

         calc_dudn_const(data, physPointsSide, jacobianSide, *cellType, cellDims, elem_side);
         break;

      case PRESS:

         calc_press(data, physPointsSide, jacobianSide, *cellType, cellDims, elem_side);
         break;

      case BASAL:

#ifdef ALBANY_FELIX
         calc_dudn_basal(data, betaOnSide, thicknessOnSide, bedTopoOnSide, dofSideVec, jacobianSide, *cellType, cellDims, elem_side);
#endif
         break;

      case BASAL_SCALAR_FIELD:

#ifdef ALBANY_FELIX
         calc_dudn_basal_scalar_field(data, betaOnSide, dofSideVec, jacobianSide, *cellType, cellDims, elem_side);
#endif
         break;

      case LATERAL:

#ifdef ALBANY_FELIX
             calc_dudn_lateral(data, thicknessOnSide, elevationOnSide, dofSideVec, jacobianSide, *cellType, cellDims, elem_side);
#endif
             break;

      case TRACTION:

         calc_traction_components(data, physPointsSide, jacobianSide, *cellType, cellDims, elem_side);
         break;

      default:

         calc_gradu_dotn_const(data, physPointsSide, jacobianSide, *cellType, cellDims, elem_side);
         break;

    }

    // Put this side's contribution into the vector

    for (std::size_t node=0; node < numNodes; ++node)
      for (std::size_t qp=0; qp < numQPsSide; ++qp)
         for (std::size_t dim=0; dim < numDOFsSet; ++dim)
           neumann(elem_LID, node, dim) +=
                  data(0, qp, dim) * weighted_trans_basis_refPointsSide(0, node, qp);


  }
}

template<typename EvalT, typename Traits>
typename NeumannBase<EvalT, Traits>::ScalarT&
NeumannBase<EvalT, Traits>::
getValue(const std::string &n) {

  if(std::string::npos != n.find("robin")) {
    for(int i = 0; i < 3; i++) {
      std::stringstream ss; ss << name << "[" << i << "]";
      if (n == ss.str())  return robin_vals[i];
    }
  }
  else if(std::string::npos != n.find("basal")) {
    for(int i = 0; i < 5; i++) {
      std::stringstream ss; ss << name << "[" << i << "]";
      if (n == ss.str())  return robin_vals[i];
    }
  }
  else {
    for(int i = 0; i < dudx.size(); i++) {
      std::stringstream ss; ss << name << "[" << i << "]";
      if (n == ss.str())  return dudx[i];
    }
  }

//  if (n == name) return const_val;
  return const_val;

}


template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
calc_traction_components(Intrepid2::FieldContainer<ScalarT> & qp_data_returned,
                          const Intrepid2::FieldContainer<MeshScalarT>& phys_side_cub_points,
                          const Intrepid2::FieldContainer<MeshScalarT>& jacobian_side_refcell,
                          const shards::CellTopology & celltopo,
                          const int cellDims,
                          int local_side_id){

  int numCells = qp_data_returned.dimension(0); // How many cell's worth of data is being computed?
  int numPoints = qp_data_returned.dimension(1); // How many QPs per cell?
  int numDOFs = qp_data_returned.dimension(2); // How many DOFs per node to calculate?

  Intrepid2::FieldContainer<ScalarT> traction(numCells, numPoints, cellDims);

/*
  double traction[3];
  traction[0] = 1.0; // x component of traction
  traction[1] = 0.0; // y component of traction
  traction[2] = 0.0; // z component of traction
*/

  for(int cell = 0; cell < numCells; cell++)
    for(int pt = 0; pt < numPoints; pt++)
      for(int dim = 0; dim < cellDims; dim++)
        traction(cell, pt, dim) = dudx[dim];

  for(int pt = 0; pt < numPoints; pt++)
    for(int dim = 0; dim < numDOFsSet; dim++)
      qp_data_returned(0, pt, dim) = -traction(0, pt, dim);

}

template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
calc_gradu_dotn_const(Intrepid2::FieldContainer<ScalarT> & qp_data_returned,
                          const Intrepid2::FieldContainer<MeshScalarT>& phys_side_cub_points,
                          const Intrepid2::FieldContainer<MeshScalarT>& jacobian_side_refcell,
                          const shards::CellTopology & celltopo,
                          const int cellDims,
                          int local_side_id){

  int numCells = qp_data_returned.dimension(0); // How many cell's worth of data is being computed?
  int numPoints = qp_data_returned.dimension(1); // How many QPs per cell?
  int numDOFs = qp_data_returned.dimension(2); // How many DOFs per node to calculate?

  Intrepid2::FieldContainer<ScalarT> grad_T(numCells, numPoints, cellDims);
  Intrepid2::FieldContainer<MeshScalarT> side_normals(numCells, numPoints, cellDims);
  Intrepid2::FieldContainer<MeshScalarT> normal_lengths(numCells, numPoints);

/*
  double kdTdx[3];
  kdTdx[0] = 1.0; // Neumann component in the x direction
  kdTdx[1] = 0.0; // Neumann component in the y direction
  kdTdx[2] = 0.0; // Neumann component in the z direction
*/

  for(int cell = 0; cell < numCells; cell++)
    for(int pt = 0; pt < numPoints; pt++)
      for(int dim = 0; dim < cellDims; dim++)
        grad_T(cell, pt, dim) = dudx[dim]; // k grad T in the x direction goes in the x spot, and so on

  // for this side in the reference cell, get the components of the normal direction vector
  Intrepid2::CellTools<MeshScalarT>::getPhysicalSideNormals(side_normals, jacobian_side_refcell,
    local_side_id, celltopo);

  // scale normals (unity)
  Intrepid2::RealSpaceTools<MeshScalarT>::vectorNorm(normal_lengths, side_normals, Intrepid2::NORM_TWO);
  Intrepid2::FunctionSpaceTools::scalarMultiplyDataData<MeshScalarT>(side_normals, normal_lengths,
    side_normals, true);

  // take grad_T dotted with the unit normal
//  Intrepid2::FunctionSpaceTools::dotMultiplyDataData<ScalarT>(qp_data_returned,
//    grad_T, side_normals);

  for(int pt = 0; pt < numPoints; pt++)
    for(int dim = 0; dim < numDOFsSet; dim++)
      qp_data_returned(0, pt, dim) = grad_T(0, pt, dim) * side_normals(0, pt, dim);

}

template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
calc_dudn_const(Intrepid2::FieldContainer<ScalarT> & qp_data_returned,
                          const Intrepid2::FieldContainer<MeshScalarT>& phys_side_cub_points,
                          const Intrepid2::FieldContainer<MeshScalarT>& jacobian_side_refcell,
                          const shards::CellTopology & celltopo,
                          const int cellDims,
                          int local_side_id,
                          ScalarT scale){

  int numCells = qp_data_returned.dimension(0); // How many cell's worth of data is being computed?
  int numPoints = qp_data_returned.dimension(1); // How many QPs per cell?
  int numDOFs = qp_data_returned.dimension(2); // How many DOFs per node to calculate?

  //std::cout << "DEBUG: applying const dudn to sideset " << this->sideSetID << ": " << (const_val * scale) << std::endl;

  for(int pt = 0; pt < numPoints; pt++)
    for(int dim = 0; dim < numDOFsSet; dim++)
      qp_data_returned(0, pt, dim) = -const_val * scale; // User directly specified dTdn, just use it


}

template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
calc_dudn_robin(Intrepid2::FieldContainer<ScalarT> & qp_data_returned,
                const Intrepid2::FieldContainer<MeshScalarT>& phys_side_cub_points,
                const Intrepid2::FieldContainer<ScalarT>& dof_side,
                const Intrepid2::FieldContainer<MeshScalarT>& jacobian_side_refcell,
                const shards::CellTopology & celltopo,
                const int cellDims,
                int local_side_id,
                ScalarT scale,
                const ScalarT* robin_param_values){

  int numCells = qp_data_returned.dimension(0); // How many cell's worth of data is being computed?
  int numPoints = qp_data_returned.dimension(1); // How many QPs per cell?
  int numDOFs = qp_data_returned.dimension(2); // How many DOFs per node to calculate?

  const ScalarT& dof_value = robin_vals[0];
  const ScalarT& coeff = robin_vals[1];
  const ScalarT& jump = robin_vals[2];

  for(int pt = 0; pt < numPoints; pt++)
    for(int dim = 0; dim < numDOFsSet; dim++)
      qp_data_returned(0, pt, dim) = coeff*(dof_side(0,pt) - dof_value) - jump * scale * 2.0;
         // mult by 2 to emulate behavior of an internal side within a single material (element block)
         //  in which case usual Neumann would add contributions from both sides, giving factor of 2
}


template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
calc_press(Intrepid2::FieldContainer<ScalarT> & qp_data_returned,
                          const Intrepid2::FieldContainer<MeshScalarT>& phys_side_cub_points,
                          const Intrepid2::FieldContainer<MeshScalarT>& jacobian_side_refcell,
                          const shards::CellTopology & celltopo,
                          const int cellDims,
                          int local_side_id){

  int numCells = qp_data_returned.dimension(0); // How many cell's worth of data is being computed?
  int numPoints = qp_data_returned.dimension(1); // How many QPs per cell?
  int numDOFs = qp_data_returned.dimension(2); // How many DOFs per node to calculate?

  Intrepid2::FieldContainer<MeshScalarT> side_normals(numCells, numPoints, cellDims);
  Intrepid2::FieldContainer<MeshScalarT> normal_lengths(numCells, numPoints);
  Intrepid2::FieldContainer<MeshScalarT> ref_normal(cellDims);

  // for this side in the reference cell, get the components of the normal direction vector
  Intrepid2::CellTools<MeshScalarT>::getPhysicalSideNormals(side_normals, jacobian_side_refcell,
    local_side_id, celltopo);

  // for this side in the reference cell, get the constant normal vector to the side for area calc
  Intrepid2::CellTools<MeshScalarT>::getReferenceSideNormal(ref_normal, local_side_id, celltopo);
  /* Note: if the side is 1D the length of the normal times 2 is the side length
     If the side is a 2D quad, the length of the normal is the area of the side
     If the side is a 2D triangle, the length of the normal times 1/2 is the area of the side
   */

  MeshScalarT area =
    Intrepid2::RealSpaceTools<MeshScalarT>::vectorNorm(ref_normal, Intrepid2::NORM_TWO);

  // Calculate proper areas

  switch(side_type[local_side_id]){

    case LINE:

      area *= 2;
      break;

    case TRI:

      area /= 2;
      break;

    case QUAD:

      break;

    default:
      TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
             "Need to supply area function for boundary type: " << side_type[local_side_id] << std::endl);
      break;

  }

  // scale normals (unity)
  Intrepid2::RealSpaceTools<MeshScalarT>::vectorNorm(normal_lengths, side_normals, Intrepid2::NORM_TWO);
  Intrepid2::FunctionSpaceTools::scalarMultiplyDataData<MeshScalarT>(side_normals, normal_lengths,
    side_normals, true);

  // Pressure is a force of magnitude P along the normal to the side, divided by the side area (det)

  for(int cell = 0; cell < numCells; cell++)
    for(int pt = 0; pt < numPoints; pt++)
      for(int dim = 0; dim < numDOFsSet; dim++)
//        qp_data_returned(cell, pt, dim) = const_val * side_normals(cell, pt, dim);
        qp_data_returned(cell, pt, dim) = const_val * side_normals(cell, pt, dim) / area;


}


template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
calc_dudn_basal(Intrepid2::FieldContainer<ScalarT> & qp_data_returned,
                                  const Intrepid2::FieldContainer<ScalarT>& basalFriction_side,
                                  const Intrepid2::FieldContainer<ScalarT>& thickness_side,
                                  const Intrepid2::FieldContainer<ScalarT>& bedTopography_side,
                                  const Intrepid2::FieldContainer<ScalarT>& dof_side,
                          const Intrepid2::FieldContainer<MeshScalarT>& jacobian_side_refcell,
                          const shards::CellTopology & celltopo,
                          const int cellDims,
                          int local_side_id){

  int numCells = qp_data_returned.dimension(0); // How many cell's worth of data is being computed?
  int numPoints = qp_data_returned.dimension(1); // How many QPs per cell?
  int numDOFs = qp_data_returned.dimension(2); // How many DOFs per node to calculate?

  //std::cout << "DEBUG: applying const dudn to sideset " << this->sideSetID << ": " << (const_val * scale) << std::endl;

  const ScalarT& beta = robin_vals[0];
  const ScalarT& alpha = robin_vals[1];
  const ScalarT& beta1 = robin_vals[2];
  const ScalarT& beta2 = robin_vals[3];
  const ScalarT& beta3 = robin_vals[4];

  Intrepid2::FieldContainer<MeshScalarT> side_normals(numCells, numPoints, cellDims);
  Intrepid2::FieldContainer<MeshScalarT> normal_lengths(numCells, numPoints);

  // for this side in the reference cell, get the components of the normal direction vector
  Intrepid2::CellTools<MeshScalarT>::getPhysicalSideNormals(side_normals, jacobian_side_refcell,
    local_side_id, celltopo);

  // scale normals (unity)
  Intrepid2::RealSpaceTools<MeshScalarT>::vectorNorm(normal_lengths, side_normals, Intrepid2::NORM_TWO);
  Intrepid2::FunctionSpaceTools::scalarMultiplyDataData<MeshScalarT>(side_normals, normal_lengths,
    side_normals, true);

  const double a = 1.0;
  const double Atmp = 1.0;
  const double ntmp = 3.0;
  if (beta_type == CONSTANT) {//basal (robin) condition indepenent of space
    betaXY = 1.0;
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        for(int dim = 0; dim < numDOFsSet; dim++) {
          qp_data_returned(cell, pt, dim) = betaXY*beta*dof_side(cell, pt,dim) - alpha; // d(stress)/dn = beta*u + alpha
        }
      }
    }
  }
  if (beta_type == SCALAR_FIELD) {//basal (robin) condition indepenent of space
      betaXY = 1.0;
    
      if(useStereographicMap)
      {
        double R = stereographicMapList->get<double>("Earth Radius", 6371);
        double x_0 = stereographicMapList->get<double>("X_0", 0);//-136);
        double y_0 = stereographicMapList->get<double>("Y_0", 0);//-2040);
        double R2 = std::pow(R,2);

        for(int cell = 0; cell < numCells; cell++) {
          for(int pt = 0; pt < numPoints; pt++) {
            MeshScalarT x = physPointsSide(cell,pt,0) - x_0;
            MeshScalarT y = physPointsSide(cell,pt,1) - y_0;
            MeshScalarT h = 4.0*R2/(4.0*R2 + x*x + y*y);
            MeshScalarT h2 = h*h;
            for(int dim = 0; dim < numDOFsSet; dim++) {
              qp_data_returned(cell, pt, dim) = betaXY*basalFriction_side(cell, pt)*dof_side(cell, pt,dim)*h2; // d(stress)/dn = beta*u + alpha
            }
          }
        }
      }
      else {
        for(int cell = 0; cell < numCells; cell++) {
          for(int pt = 0; pt < numPoints; pt++) {
            for(int dim = 0; dim < numDOFsSet; dim++) {
              qp_data_returned(cell, pt, dim) = betaXY*basalFriction_side(cell, pt)*dof_side(cell, pt,dim); // d(stress)/dn = beta*u + alpha
            }
          }
        }
      }
  }
  else if (beta_type == EXP_SCALAR_FIELD) {//basal (robin) condition indepenent of space
      betaXY = 1.0;

      if(useStereographicMap)
      {
        double R = stereographicMapList->get<double>("Earth Radius", 6371);
        double x_0 = stereographicMapList->get<double>("X_0", 0);//-136);
        double y_0 = stereographicMapList->get<double>("Y_0", 0);//-2040);
        double R2 = std::pow(R,2);

        for(int cell = 0; cell < numCells; cell++) {
          for(int pt = 0; pt < numPoints; pt++) {
            MeshScalarT x = physPointsSide(cell,pt,0) - x_0;
            MeshScalarT y = physPointsSide(cell,pt,1) - y_0;
            MeshScalarT h = 4.0*R2/(4.0*R2 + x*x + y*y);
            MeshScalarT h2 = h*h;
            for(int dim = 0; dim < numDOFsSet; dim++) {
              qp_data_returned(cell, pt, dim) = betaXY*std::exp(basalFriction_side(cell, pt))*dof_side(cell, pt,dim)*h2; // d(stress)/dn = beta*u + alpha
            }
          }
        }
      }
      else {
        for(int cell = 0; cell < numCells; cell++) {
          for(int pt = 0; pt < numPoints; pt++) {
            for(int dim = 0; dim < numDOFsSet; dim++) {
              qp_data_returned(cell, pt, dim) = betaXY*std::exp(basalFriction_side(cell, pt))*dof_side(cell, pt,dim); // d(stress)/dn = beta*u + alpha
            }
          }
        }
      }
  }
  else if (beta_type == POWERLAW_SCALAR_FIELD) {//basal (robin) condition indepenent of space
        betaXY = 1.0;

        if(useStereographicMap)
        {
          double R = stereographicMapList->get<double>("Earth Radius", 6371);
          double x_0 = stereographicMapList->get<double>("X_0", 0);//-136);
          double y_0 = stereographicMapList->get<double>("Y_0", 0);//-2040);
          double R2 = std::pow(R,2);

          for(int cell = 0; cell < numCells; cell++) {
            for(int pt = 0; pt < numPoints; pt++) {
              MeshScalarT x = physPointsSide(cell,pt,0) - x_0;
              MeshScalarT y = physPointsSide(cell,pt,1) - y_0;
              MeshScalarT h = 4.0*R2/(4.0*R2 + x*x + y*y);
              MeshScalarT h2 = h*h;
              ScalarT vel=0;
              const ScalarT beta = basalFriction_side(cell, pt);//*(thickness_side(cell, pt)*rho > -bedTopography_side(cell,pt)*rho_w);
              for(int dim = 0; dim < numDOFsSet; dim++)
                vel += dof_side(cell, pt,dim)*dof_side(cell, pt,dim);
              for(int dim = 0; dim < numDOFsSet; dim++) {
                qp_data_returned(cell, pt, dim) = betaXY*beta*std::pow(vel+1e-6, (1./3.-1.)/2.)*dof_side(cell, pt,dim)*h2; // d(stress)/dn = beta*u + alpha
              }
            }
          }
        }
        else {
          for(int cell = 0; cell < numCells; cell++) {
            for(int pt = 0; pt < numPoints; pt++) {
              ScalarT vel=0;
              const ScalarT beta = basalFriction_side(cell, pt);//*(thickness_side(cell, pt)*rho > -bedTopography_side(cell,pt)*rho_w);
              for(int dim = 0; dim < numDOFsSet; dim++)
                vel += dof_side(cell, pt,dim)*dof_side(cell, pt,dim);
              for(int dim = 0; dim < numDOFsSet; dim++) {
                qp_data_returned(cell, pt, dim) = betaXY*beta*std::pow(vel+1e-6, (1./3.-1.)/2.)*dof_side(cell, pt,dim); // d(stress)/dn = beta*u + alpha
              }
            }
          }
        }
    }
  if (beta_type == GLP_SCALAR_FIELD) {//basal (robin) condition indepenent of space
      betaXY = 1;

      if(useStereographicMap)
      {
        double R = stereographicMapList->get<double>("Earth Radius", 6371);
        double x_0 = stereographicMapList->get<double>("X_0", 0);//-136);
        double y_0 = stereographicMapList->get<double>("Y_0", 0);//-2040);
        double R2 = std::pow(R,2);

        for(int cell = 0; cell < numCells; cell++) {
          for(int pt = 0; pt < numPoints; pt++) {
            MeshScalarT x = physPointsSide(cell,pt,0) - x_0;
            MeshScalarT y = physPointsSide(cell,pt,1) - y_0;
            MeshScalarT h = 4.0*R2/(4.0*R2 + x*x + y*y);
            MeshScalarT h2 = h*h;
            const ScalarT beta = basalFriction_side(cell, pt)*(thickness_side(cell, pt)*rho > - bedTopography_side(cell,pt)*rho_w);
            for(int dim = 0; dim < numDOFsSet; dim++) {
              qp_data_returned(cell, pt, dim) = betaXY*beta*dof_side(cell, pt,dim)*h2; // d(stress)/dn = beta*u + alpha
            }
          }
        }
      }
      else {
        for(int cell = 0; cell < numCells; cell++) {
          for(int pt = 0; pt < numPoints; pt++) {
            ScalarT vel=0;
            const ScalarT beta = basalFriction_side(cell, pt)*(thickness_side(cell, pt)*rho > - bedTopography_side(cell,pt)*rho_w);
            for(int dim = 0; dim < numDOFsSet; dim++)
              vel += dof_side(cell, pt,dim)*dof_side(cell, pt,dim);
            for(int dim = 0; dim < numDOFsSet; dim++) {
              qp_data_returned(cell, pt, dim) = betaXY*beta*std::pow(vel+1e-6, (1./3.-1.)/2.)*dof_side(cell, pt,dim); // d(stress)/dn = beta*u + alpha
            }
          }
        }
      }
  }
  else if (beta_type == EXP_SCALAR_FIELD_THK) {//basal (robin) condition indepenent of space
      betaXY = 1.0;

      if(useStereographicMap)
      {
        double R = stereographicMapList->get<double>("Earth Radius", 6371);
        double x_0 = stereographicMapList->get<double>("X_0", 0);//-136);
        double y_0 = stereographicMapList->get<double>("Y_0", 0);//-2040);
        double R2 = std::pow(R,2);

        for(int cell = 0; cell < numCells; cell++) {
          for(int pt = 0; pt < numPoints; pt++) {
            MeshScalarT x = physPointsSide(cell,pt,0) - x_0;
            MeshScalarT y = physPointsSide(cell,pt,1) - y_0;
            MeshScalarT h = 4.0*R2/(4.0*R2 + x*x + y*y);
            MeshScalarT h2 = h*h;
            for(int dim = 0; dim < numDOFsSet; dim++) {
              qp_data_returned(cell, pt, dim) = betaXY*std::exp(basalFriction_side(cell, pt))*thickness_side(cell, pt)*dof_side(cell, pt,dim)*h2; // d(stress)/dn = beta*u + alpha
            }
          }
        }
      }
      else {
        for(int cell = 0; cell < numCells; cell++) {
          for(int pt = 0; pt < numPoints; pt++) {
            for(int dim = 0; dim < numDOFsSet; dim++) {
              qp_data_returned(cell, pt, dim) = betaXY*std::exp(basalFriction_side(cell, pt))*thickness_side(cell, pt)*dof_side(cell, pt,dim); // d(stress)/dn = beta*u + alpha
            }
          }
        }
      }
  }
  else if (beta_type == EXPTRIG) {
    const double a = 1.0;
    const double A = 1.0;
    const double n = L;
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        for(int dim = 0; dim < numDOFsSet; dim++) {
          MeshScalarT x = physPointsSide(cell,pt,0);
          MeshScalarT y2pi = 2.0*pi*physPointsSide(cell,pt,1);
          MeshScalarT muargt = (a*a + 4.0*pi*pi - 2.0*pi*a)*sin(y2pi)*sin(y2pi) + 1.0/4.0*(2.0*pi+a)*(2.0*pi+a)*cos(y2pi)*cos(y2pi);
          muargt = sqrt(muargt)*exp(a*x);
          betaXY = 1.0/2.0*pow(A, -1.0/n)*pow(muargt, 1.0/n - 1.0);
          qp_data_returned(cell, pt, dim) = betaXY*beta*dof_side(cell, pt,dim) - alpha*side_normals(cell,pt,dim); // d(stress)/dn = beta*u + alpha
        }
      }
  }
 }
 else if (beta_type == ISMIP_HOM_TEST_C) {
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        for(int dim = 0; dim < numDOFsSet; dim++) {
          MeshScalarT x = physPointsSide(cell,pt,0);
          MeshScalarT y = physPointsSide(cell,pt,1);
          betaXY = 1.0 + sin(2.0*pi/L*x)*sin(2.0*pi/L*y);
          qp_data_returned(cell, pt, dim) = betaXY*beta*dof_side(cell, pt,dim) - alpha*side_normals(cell,pt,dim); // d(stress)/dn = beta*u + alpha
        }
      }
  }
 }
 else if (beta_type == ISMIP_HOM_TEST_D) {
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        for(int dim = 0; dim < numDOFsSet; dim++) {
          MeshScalarT x = physPointsSide(cell,pt,0);
          betaXY = 1.0 + sin(2.0*pi/L*x);
          qp_data_returned(cell, pt, dim) = betaXY*beta*dof_side(cell, pt,dim) - alpha*side_normals(cell,pt,dim); // d(stress)/dn = beta*u + alpha
        }
      }
  }
 }
 else if (beta_type == CONFINEDSHELF) {
    const double s = 0.06;
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        for(int dim = 0; dim < numDOFsSet; dim++) {
          MeshScalarT z = physPointsSide(cell,pt,2);
          if (z > 0.0)
            betaXY = 0.0;
          else
            betaXY = -z; //betaXY = depth in km
          qp_data_returned(cell, pt, dim) = -(beta*(s-z) + alpha*betaXY); // d(stress)/dn = beta*(s-z)+alpha*(-z)
        }
      }
  }
 }
 else if (beta_type == CIRCULARSHELF) {
    const double s = 0.11479;
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        for(int dim = 0; dim < numDOFsSet; dim++) {
          MeshScalarT z = physPointsSide(cell,pt,2);
          if (z > 0.0)
            betaXY = 0.0;
          else
            betaXY = -z; //betaXY = depth in km
          qp_data_returned(cell, pt, dim) = -(beta*(s-z) + alpha*betaXY)*side_normals(cell,pt,dim); // d(stress)/dn = (beta*(s-z)+alpha*(-z))*n_i
        }
      }
  }
 }
 else if (beta_type == DOMEUQ) {
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        for(int dim = 0; dim < numDOFsSet; dim++) {
          MeshScalarT x = physPointsSide(cell,pt,0);
          MeshScalarT y = physPointsSide(cell,pt,1);
          MeshScalarT r = sqrt(x*x+y*y);
          qp_data_returned(cell, pt, dim) = (alpha + beta1*x + beta2*y + beta3*r)*dof_side(cell,pt,dim); // d(stress)/dn = (alpha + beta1*x + beta2*y + beta3*r)*u;
        }
      }
  }
 }
 //Robin/Neumann bc for FELIX FO XZ MMS test case
 else if (beta_type == FELIX_XZ_MMS) {
    //parameter values are hard-coded here...
    MeshScalarT H = 1.0; 
    double alpha0 = 4.0e-5; 
    double beta0 = 1;
    double rho_g = 910.0*9.8; 
    double s0 = 2.0;
    double A = 1e-4; //CAREFUL! A is hard-coded here, needs to match input file!!
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        for(int dim = 0; dim < numDOFsSet; dim++) {
          MeshScalarT x = physPointsSide(cell,pt,0);
          MeshScalarT z = physPointsSide(cell,pt,1);
          MeshScalarT s = s0 - alpha0*x*x;  //s = s0-alpha*x^2
          MeshScalarT phi1 = z - s; //phi1 = z-s
          //phi2 = 4*A*alpha^3*rho^3*g^3*x
          MeshScalarT phi2 = 4.0*A*pow(alpha0*rho_g, 3)*x;  
          //phi3 = 4*x^3*phi1^5*phi2^2
          MeshScalarT phi3 = 4.0*x*x*x*pow(phi1,5)*phi2*phi2; 
          //phi4 = 8*alpha*x^3*phi1^3*phi2 - (2*H*alpha*rho*g)/beta + 3*x*phi2*(phi1^4-H^4)
          MeshScalarT phi4 = 8.0*alpha0*pow(x,3)*pow(phi1,3)*phi2 - 2.0*H*alpha0*rho_g/beta0 + 3.0*x*phi2*(pow(phi1,4) - pow(H,4));
          //phi5 = 56*alpha*x^2*phi1^3*phi2 + 48*alpha^2*x^4*phi1^2*phi2 + 6*phi2*(phi1^4-H^4
          MeshScalarT phi5 = 56.0*alpha0*x*x*pow(phi1,3)*phi2 + 48.0*alpha0*alpha0*pow(x,4)*phi1*phi1*phi2 
                           + 6.0*phi2*(pow(phi1,4) - pow(H,4)); 
          //mu = 1/2*(A*phi4^2 + A*x*phi1*phi3)^(-1/3) -- this is mu but with A factored out
           MeshScalarT mu = 0.5*pow(A*phi4*phi4 + A*x*phi1*phi3, -1.0/3.0); 
           // d(stress)/dn = beta0*u + 4*phi4*mutilde*beta1*nx - 4*phi2*x^2*phi1^3*mutilde*beta2*ny 
           //              + (2*H*alpha*rho*g*x - beta0*x^2*phi2*(phi1^4 - H^4)*alpha;
          qp_data_returned(cell, pt, dim) = beta*dof_side(cell,pt,dim)
                                           + 4.0*phi4*mu*alpha*side_normals(cell,pt,0)
                                           + 4.0*phi2*x*x*pow(phi1,3)*mu*beta1*side_normals(cell,pt,1)
                                           - (2.0*H*alpha0*rho_g*x - beta0*x*x*phi2*(pow(phi1,4) - pow(H,4)))*beta2;
        }
      }
  }
 }
}

template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
calc_dudn_basal_scalar_field(Intrepid2::FieldContainer<ScalarT> & qp_data_returned,
                                  const Intrepid2::FieldContainer<ScalarT>& basalFriction_side,
                                  const Intrepid2::FieldContainer<ScalarT>& dof_side,
                          const Intrepid2::FieldContainer<MeshScalarT>& jacobian_side_refcell,
                          const shards::CellTopology & celltopo,
                          const int cellDims,
                          int local_side_id){

  int numCells = qp_data_returned.dimension(0); // How many cell's worth of data is being computed?
  int numPoints = qp_data_returned.dimension(1); // How many QPs per cell?
  int numDOFs = qp_data_returned.dimension(2); // How many DOFs per node to calculate?

  //std::cout << "DEBUG: applying const dudn to sideset " << this->sideSetID << ": " << (const_val * scale) << std::endl;

  const ScalarT& scale = robin_vals[0];

  Intrepid2::FieldContainer<MeshScalarT> side_normals(numCells, numPoints, cellDims);
  Intrepid2::FieldContainer<MeshScalarT> normal_lengths(numCells, numPoints);

  // for this side in the reference cell, get the components of the normal direction vector
  Intrepid2::CellTools<MeshScalarT>::getPhysicalSideNormals(side_normals, jacobian_side_refcell,
    local_side_id, celltopo);

  // scale normals (unity)
  Intrepid2::RealSpaceTools<MeshScalarT>::vectorNorm(normal_lengths, side_normals, Intrepid2::NORM_TWO);
  Intrepid2::FunctionSpaceTools::scalarMultiplyDataData<MeshScalarT>(side_normals, normal_lengths,
    side_normals, true);

  for(int cell = 0; cell < numCells; cell++) {
    for(int pt = 0; pt < numPoints; pt++) {
      for(int dim = 0; dim < numDOFsSet; dim++) {
        qp_data_returned(cell, pt, dim) = scale*basalFriction_side(cell, pt)*dof_side(cell, pt,dim); // d(stress)/dn = scale*beta*u
      }
    }
  }
}

template<typename EvalT, typename Traits>
void NeumannBase<EvalT, Traits>::
calc_dudn_lateral(Intrepid2::FieldContainer<ScalarT> & qp_data_returned,
                                  const Intrepid2::FieldContainer<ScalarT>& thickness_side,
                                  const Intrepid2::FieldContainer<ScalarT>& elevation_side,
                                  const Intrepid2::FieldContainer<ScalarT>& dof_side,
                          const Intrepid2::FieldContainer<MeshScalarT>& jacobian_side_refcell,
                          const shards::CellTopology & celltopo,
                          const int cellDims,
                          int local_side_id){

  int numCells = qp_data_returned.dimension(0); // How many cell's worth of data is being computed?
  int numPoints = qp_data_returned.dimension(1); // How many QPs per cell?

  //std::cout << "DEBUG: applying const dudn to sideset " << this->sideSetID << ": " << (const_val * scale) << std::endl;

  Intrepid2::FieldContainer<MeshScalarT> side_normals(numCells, numPoints, cellDims);
  Intrepid2::FieldContainer<MeshScalarT> normal_lengths(numCells, numPoints);

  // for this side in the reference cell, get the components of the normal direction vector
  Intrepid2::CellTools<MeshScalarT>::getPhysicalSideNormals(side_normals, jacobian_side_refcell,
    local_side_id, celltopo);

  // scale normals (unity)
  Intrepid2::RealSpaceTools<MeshScalarT>::vectorNorm(normal_lengths, side_normals, Intrepid2::NORM_TWO);
  Intrepid2::FunctionSpaceTools::scalarMultiplyDataData<MeshScalarT>(side_normals, normal_lengths,
    side_normals, true);

  const ScalarT &immersedRatioProvided = robin_vals[0];
  if (beta_type == LATERAL_BACKPRESSURE)  {
    for(int cell = 0; cell < numCells; cell++) {
      for(int pt = 0; pt < numPoints; pt++) {
        ScalarT H = thickness_side(cell, pt);
        ScalarT s = elevation_side(cell, pt);
        ScalarT immersedRatio = 0.;
        if (immersedRatioProvided == 0) { //default case: immersedRatio calculated inside the code from s and H
          if (H > 1e-8) { //make sure H is not too small
            ScalarT ratio = s/H;
            if(ratio < 0.)          //ice is completely under sea level
              immersedRatio = 1;
            else if(ratio < 1)      //ice is partially under sea level
              immersedRatio = 1. - ratio;
          }
         }
         else {
           immersedRatio = immersedRatioProvided; //alteranate case: immersedRatio is set to some value given in the input file
         }
        ScalarT normalStress = - 0.5 * g *  H * (rho - rho_w * immersedRatio*immersedRatio);
        for(int dim = 0; dim < numDOFsSet; dim++)
          qp_data_returned(cell, pt, dim) =  normalStress * side_normals(cell,pt,dim);
      }
    }
    if(useStereographicMap) {
      double R = stereographicMapList->get<double>("Earth Radius", 6371);
      double x_0 = stereographicMapList->get<double>("X_0", 0);//-136);
      double y_0 = stereographicMapList->get<double>("Y_0", 0);//-2040);
      double R2 = std::pow(R,2);
      for(int cell = 0; cell < numCells; cell++) {
        for(int pt = 0; pt < numPoints; pt++) {
          MeshScalarT x = physPointsSide(cell,pt,0) - x_0;
          MeshScalarT y = physPointsSide(cell,pt,1) -y_0;
          MeshScalarT h = 4.0*R2/(4.0*R2 + x*x + y*y);
          for(int dim = 0; dim < numDOFsSet; dim++)
            qp_data_returned(cell, pt, dim) *= h; 
        }
      }
    }
  }
}

// **********************************************************************
// Specialization: Residual
// **********************************************************************
template<typename Traits>
Neumann<PHAL::AlbanyTraits::Residual,Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::Residual,Traits>(p)
{
}



template<typename Traits>
void Neumann<PHAL::AlbanyTraits::Residual, Traits>::
evaluateFields(typename Traits::EvalData workset)
{

  Teuchos::RCP<Tpetra_Vector> fT = workset.fT;
  Teuchos::ArrayRCP<ST> fT_nonconstView = fT->get1dViewNonConst();

  // Fill in "neumann" array
  this->evaluateNeumannContribution(workset);

  // Place it at the appropriate offset into F
  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];


    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){
        fT_nonconstView[nodeID[node][this->offset[dim]]] += this->neumann(cell, node, dim);
    }
  }
}

// **********************************************************************
// Specialization: Jacobian
// **********************************************************************

template<typename Traits>
Neumann<PHAL::AlbanyTraits::Jacobian, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::Jacobian,Traits>(p)
{
}
// **********************************************************************
#ifdef ALBANY_KOKKOS_UNDER_DEVELOPMENT
template<typename Traits>
KOKKOS_INLINE_FUNCTION
void Neumann<PHAL::AlbanyTraits::Jacobian,Traits>::
operator()(const Newmann_Tag& tag, const int& cell) const
{

  LO colT[1];
  LO rowT;
  ST value[1];
  int lcol;
  const int neq = Index.dimension(2);
  const int nunk = neq*this->numNodes;
  

  for (std::size_t node = 0; node < this->numNodes; ++node)
    for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

      int dim2=this->offset[dim];
      rowT = Index(cell,node,dim2);

      if (this->fT != Teuchos::null) {
         this->fT->sumIntoLocalValue(rowT, this->neumann(cell, node, dim).val());
      }

        // Check derivative array is nonzero
        if (this->neumann(cell, node, dim).hasFastAccess()) {
          // Loop over nodes in element
          for (unsigned int node_col=0; node_col<this->numNodes; node_col++){
        
            // Loop over equations per node
            for (unsigned int eq_col=0; eq_col<neq; eq_col++) {
             lcol = neq * node_col + eq_col;

            // Global column
            colT[0] =  Index(cell, node_col, eq_col);
            value[0] = this->neumann(cell, node, dim).fastAccessDx(lcol);
            if (is_adjoint) {
              // Sum Jacobian transposed
              jacobian.sumIntoValues(colT[0], &rowT,1, &value[0],true);
            }
            else {
              // Sum Jacobian
              jacobian.sumIntoValues(rowT, colT, nunk,value, true);
            }
          } // column equations
        } // column nodes
      } // has fast access
    }
          
 }
#endif
// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::Jacobian, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
#ifndef ALBANY_KOKKOS_UNDER_DEVELOPMENT
  Teuchos::RCP<Tpetra_Vector> fT = workset.fT;
  Teuchos::ArrayRCP<ST> fT_nonconstView = fT->get1dViewNonConst();
  Teuchos::RCP<Tpetra_CrsMatrix> JacT = workset.JacT;


  // Fill in "neumann" array
  this->evaluateNeumannContribution(workset);
  int lcol;
  Teuchos::Array<LO> rowT(1);
  Teuchos::Array<LO> colT(1);
  Teuchos::Array<ST> value(1);

  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];

    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

      rowT[0] = nodeID[node][this->offset[dim]];

      int neq = nodeID[node].size();

      if (fT != Teuchos::null) {
         fT->sumIntoLocalValue(rowT[0], this->neumann(cell, node, dim).val());
      }

        // Check derivative array is nonzero
        if (this->neumann(cell, node, dim).hasFastAccess()) {

          // Loop over nodes in element
          for (unsigned int node_col=0; node_col<this->numNodes; node_col++){

            // Loop over equations per node
            for (unsigned int eq_col=0; eq_col<neq; eq_col++) {
              lcol = neq * node_col + eq_col;

            // Global column
            colT[0] =  nodeID[node_col][eq_col];
            value[0] = this->neumann(cell, node, dim).fastAccessDx(lcol);   
            if (workset.is_adjoint) {
              // Sum Jacobian transposed
              JacT->sumIntoLocalValues(colT[0], rowT(), value());
            }
            else {
              // Sum Jacobian
            JacT->sumIntoLocalValues(rowT[0], colT(), value());
            }
          } // column equations
        } // column nodes
      } // has fast access
    }
  }
#else
  
  fT = workset.fT;
  fT_nonconstView = fT->get1dViewNonConst();
  JacT = workset.JacT;


  // Fill in "neumann" array
  this->evaluateNeumannContribution(workset);
 
 //  if ( !JacT->isFillActive())
//    JacT->resumeFill();
 
   jacobian=JacT->getLocalMatrix();

   Index=workset.wsElNodeEqID_kokkos;

   is_adjoint=workset.is_adjoint;

   Kokkos::parallel_for(Newmann_Policy(0,workset.numCells),*this);

//   if ( !JacT->isFillActive())
//    JacT->fillComplete();

#endif
}

// **********************************************************************
// Specialization: Tangent
// **********************************************************************

template<typename Traits>
Neumann<PHAL::AlbanyTraits::Tangent, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::Tangent,Traits>(p)
{
}

// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::Tangent, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  Teuchos::RCP<Tpetra_Vector> fT = workset.fT;
  Teuchos::RCP<Tpetra_MultiVector> JVT = workset.JVT;
  Teuchos::RCP<Tpetra_MultiVector> fpT = workset.fpT;
  
  // Fill the local "neumann" array with cell contributions

  this->evaluateNeumannContribution(workset);

  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];

    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

        int row = nodeID[node][this->offset[dim]];

        if (fT != Teuchos::null)
          fT->sumIntoLocalValue(row, this->neumann(cell, node, dim).val());

        if (JVT != Teuchos::null)
          for (int col=0; col<workset.num_cols_x; col++)

            JVT->sumIntoLocalValue(row, col, this->neumann(cell, node, dim).dx(col));

        if (fpT != Teuchos::null)
          for (int col=0; col<workset.num_cols_p; col++)
            fpT->sumIntoLocalValue(row, col, this->neumann(cell, node, dim).dx(col+workset.param_offset));
      }
  }
}

// **********************************************************************
// Specialization: DistParamDeriv
// **********************************************************************

template<typename Traits>
Neumann<PHAL::AlbanyTraits::DistParamDeriv, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::DistParamDeriv,Traits>(p)
{
}

// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::DistParamDeriv, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  Teuchos::RCP<Tpetra_MultiVector> fpVT = workset.fpVT;
  bool trans = workset.transpose_dist_param_deriv;
  int num_cols = workset.VpT->getNumVectors();

  // Fill the local "neumann" array with cell contributions

  this->evaluateNeumannContribution(workset);

  if (trans) {
    int neq = workset.numEqs;
    const Albany::IDArray&  wsElDofs = workset.distParamLib->get(workset.dist_param_deriv_name)->workset_elem_dofs()[workset.wsIndex];
    for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
      const Teuchos::ArrayRCP<Teuchos::ArrayRCP<double> >& local_Vp =
        workset.local_Vp[cell];
      const int num_deriv = local_Vp.size()/neq;
      for (int i=0; i<num_deriv; i++) {
        for (int col=0; col<num_cols; col++) {
          double val = 0.0;
          for (std::size_t node = 0; node < this->numNodes; ++node) {
            for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){
              int eq = this->offset[dim];
              val += this->neumann(cell, node, dim).dx(i)*local_Vp[node*neq+eq][col];
            }
          }
          const LO row = wsElDofs((int)cell,i,0);
          fpVT->sumIntoLocalValue(row, col, val);
        }
      }
    }

  }

  else {
    for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
      const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  =
        workset.wsElNodeEqID[cell];
      const Teuchos::ArrayRCP<Teuchos::ArrayRCP<double> >& local_Vp =
        workset.local_Vp[cell];
      const int num_deriv = local_Vp.size();

      for (std::size_t node = 0; node < this->numNodes; ++node)
        for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){
          const int row = nodeID[node][this->offset[dim]];
          for (int col=0; col<num_cols; col++) {
            double val = 0.0;
            for (int i=0; i<num_deriv; ++i)
              val += this->neumann(cell, node, dim).dx(i)*local_Vp[i][col];
            fpVT->sumIntoLocalValue(row, col, val);
          }
        }
    }

  }
}

// **********************************************************************
// Specialization: Stochastic Galerkin Residual
// **********************************************************************

#ifdef ALBANY_SG
template<typename Traits>
Neumann<PHAL::AlbanyTraits::SGResidual, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::SGResidual,Traits>(p)
{
}


// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::SGResidual, Traits>::
evaluateFields(typename Traits::EvalData workset)
{

  Teuchos::RCP< Stokhos::EpetraVectorOrthogPoly > f = workset.sg_f;

  int nblock = f->size();

  // Fill the local "neumann" array with cell contributions

  this->evaluateNeumannContribution(workset);

  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];

    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

        for (int block=0; block<nblock; block++)
            (*f)[block][nodeID[node][this->offset[dim]]] += this->neumann(cell, node, dim).coeff(block);

    }
  }
}

// **********************************************************************
// Specialization: Stochastic Galerkin Jacobian
// **********************************************************************

template<typename Traits>
Neumann<PHAL::AlbanyTraits::SGJacobian, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::SGJacobian,Traits>(p)
{
}

// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::SGJacobian, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  Teuchos::RCP< Stokhos::EpetraVectorOrthogPoly > f = workset.sg_f;
  Teuchos::RCP< Stokhos::VectorOrthogPoly<Epetra_CrsMatrix> > Jac =
    workset.sg_Jac;

  // Fill the local "neumann" array with cell contributions

  this->evaluateNeumannContribution(workset);

  int row, lcol, col;
  int nblock = 0;

  if (f != Teuchos::null)
    nblock = f->size();

  int nblock_jac = Jac->size();
  double c; // use double since it goes into CrsMatrix

  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];

    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

        row = nodeID[node][this->offset[dim]];
        int neq = nodeID[node].size();

        if (f != Teuchos::null) {

          for (int block=0; block<nblock; block++)
            (*f)[block].SumIntoMyValue(row, 0, this->neumann(cell, node, dim).val().coeff(block));

        }

        // Check derivative array is nonzero
        if (this->neumann(cell, node, dim).hasFastAccess()) {

          // Loop over nodes in element
          for (unsigned int node_col=0; node_col<this->numNodes; node_col++){

            // Loop over equations per node
            for (unsigned int eq_col=0; eq_col<neq; eq_col++) {
              lcol = neq * node_col + eq_col;

              // Global column
              col =  nodeID[node_col][eq_col];

              // Sum Jacobian
              for (int block=0; block<nblock_jac; block++) {

                c = this->neumann(cell, node, dim).fastAccessDx(lcol).coeff(block);
                if (workset.is_adjoint) {

                  (*Jac)[block].SumIntoMyValues(col, 1, &c, &row);

                }
                else {

                  (*Jac)[block].SumIntoMyValues(row, 1, &c, &col);

                }
              }
            } // column equations
          } // column nodes
        } // has fast access
    }
  }
}

// **********************************************************************
// Specialization: Stochastic Galerkin Tangent
// **********************************************************************

template<typename Traits>
Neumann<PHAL::AlbanyTraits::SGTangent, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::SGTangent,Traits>(p)
{
}

// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::SGTangent, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  Teuchos::RCP< Stokhos::EpetraVectorOrthogPoly > f = workset.sg_f;
  Teuchos::RCP< Stokhos::EpetraMultiVectorOrthogPoly > JV = workset.sg_JV;
  Teuchos::RCP< Stokhos::EpetraMultiVectorOrthogPoly > fp = workset.sg_fp;

  // Fill the local "neumann" array with cell contributions

  this->evaluateNeumannContribution(workset);

  int nblock = 0;
  if (f != Teuchos::null)
    nblock = f->size();
  else if (JV != Teuchos::null)
    nblock = JV->size();
  else if (fp != Teuchos::null)
    nblock = fp->size();
  else
    TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
                       "One of sg_f, sg_JV, or sg_fp must be non-null! " <<
                       std::endl);

  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {

    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];

    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

        int row = nodeID[node][this->offset[dim]];

        if (f != Teuchos::null)
          for (int block=0; block<nblock; block++)
            (*f)[block].SumIntoMyValue(row, 0, this->neumann(cell, node, dim).val().coeff(block));

        if (JV != Teuchos::null)
          for (int col=0; col<workset.num_cols_x; col++)
            for (int block=0; block<nblock; block++)
              (*JV)[block].SumIntoMyValue(row, col, this->neumann(cell, node, dim).dx(col).coeff(block));

          for (int col=0; col<workset.num_cols_p; col++)
            for (int block=0; block<nblock; block++)
              (*fp)[block].SumIntoMyValue(row, col, this->neumann(cell, node, dim).dx(col+workset.param_offset).coeff(block));
    }
  }
}
#endif 
#ifdef ALBANY_ENSEMBLE 

// **********************************************************************
// Specialization: Multi-point Residual
// **********************************************************************

template<typename Traits>
Neumann<PHAL::AlbanyTraits::MPResidual, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::MPResidual,Traits>(p)
{
}

// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::MPResidual, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  Teuchos::RCP< Stokhos::ProductEpetraVector > f = workset.mp_f;

  // Fill the local "neumann" array with cell contributions

  this->evaluateNeumannContribution(workset);

  int nblock = f->size();
  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];

    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

        for (int block=0; block<nblock; block++)
          (*f)[block][nodeID[node][this->offset[dim]]] += this->neumann(cell, node, dim).coeff(block);

    }
  }
}

// **********************************************************************
// Specialization: Multi-point Jacobian
// **********************************************************************

template<typename Traits>
Neumann<PHAL::AlbanyTraits::MPJacobian, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::MPJacobian,Traits>(p)
{
}

// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::MPJacobian, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  Teuchos::RCP< Stokhos::ProductEpetraVector > f = workset.mp_f;
  Teuchos::RCP< Stokhos::ProductContainer<Epetra_CrsMatrix> > Jac =
    workset.mp_Jac;

  // Fill the local "neumann" array with cell contributions

  this->evaluateNeumannContribution(workset);

  int row, lcol, col;
  int nblock = 0;

  if (f != Teuchos::null)
    nblock = f->size();

  int nblock_jac = Jac->size();
  double c; // use double since it goes into CrsMatrix

  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];

    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

        row = nodeID[node][this->offset[dim]];
        int neq = nodeID[node].size();

        if (f != Teuchos::null)
          for (int block=0; block<nblock; block++)
            (*f)[block].SumIntoMyValue(row, 0, this->neumann(cell, node, dim).val().coeff(block));


        // Check derivative array is nonzero
        if (this->neumann(cell, node, dim).hasFastAccess()) {

          // Loop over nodes in element
          for (unsigned int node_col=0; node_col<this->numNodes; node_col++){

            // Loop over equations per node
            for (unsigned int eq_col=0; eq_col<neq; eq_col++) {
              lcol = neq * node_col + eq_col;

              // Global column
              col =  nodeID[node_col][eq_col];

              // Sum Jacobian
              for (int block=0; block<nblock_jac; block++) {

                c = this->neumann(cell, node, dim).fastAccessDx(lcol).coeff(block);
               (*Jac)[block].SumIntoMyValues(row, 1, &c, &col);

             }
            } // column equations
          } // column nodes
        } // has fast access
    }
  }
}

// **********************************************************************
// Specialization: Multi-point Tangent
// **********************************************************************

template<typename Traits>
Neumann<PHAL::AlbanyTraits::MPTangent, Traits>::
Neumann(Teuchos::ParameterList& p)
  : NeumannBase<PHAL::AlbanyTraits::MPTangent,Traits>(p)
{
}

// **********************************************************************
template<typename Traits>
void Neumann<PHAL::AlbanyTraits::MPTangent, Traits>::
evaluateFields(typename Traits::EvalData workset)
{
  Teuchos::RCP< Stokhos::ProductEpetraVector > f = workset.mp_f;
  Teuchos::RCP< Stokhos::ProductEpetraMultiVector > JV = workset.mp_JV;
  Teuchos::RCP< Stokhos::ProductEpetraMultiVector > fp = workset.mp_fp;

  // Fill the local "neumann" array with cell contributions

  this->evaluateNeumannContribution(workset);

  int nblock = 0;
  if (f != Teuchos::null)
    nblock = f->size();
  else if (JV != Teuchos::null)
    nblock = JV->size();
  else if (fp != Teuchos::null)
    nblock = fp->size();
  else
    TEUCHOS_TEST_FOR_EXCEPTION(true, std::logic_error,
                       "One of mp_f, mp_JV, or mp_fp must be non-null! " <<
                       std::endl);

  for (std::size_t cell=0; cell < workset.numCells; ++cell ) {
    const Teuchos::ArrayRCP<Teuchos::ArrayRCP<int> >& nodeID  = workset.wsElNodeEqID[cell];

    for (std::size_t node = 0; node < this->numNodes; ++node)
      for (std::size_t dim = 0; dim < this->numDOFsSet; ++dim){

        int row = nodeID[node][this->offset[dim]];

        if (f != Teuchos::null)
          for (int block=0; block<nblock; block++)
            (*f)[block].SumIntoMyValue(row, 0, this->neumann(cell, node, dim).val().coeff(block));

        if (JV != Teuchos::null)
          for (int col=0; col<workset.num_cols_x; col++)
            for (int block=0; block<nblock; block++)
              (*JV)[block].SumIntoMyValue(row, col, this->neumann(cell, node, dim).dx(col).coeff(block));

        if (fp != Teuchos::null)
          for (int col=0; col<workset.num_cols_p; col++)
            for (int block=0; block<nblock; block++)
              (*fp)[block].SumIntoMyValue(row, col, this->neumann(cell, node, dim).dx(col+workset.param_offset).coeff(block));

    }
  }
}
#endif


// **********************************************************************
// Simple evaluator to aggregate all Neumann BCs into one "field"
// **********************************************************************

template<typename EvalT, typename Traits>
NeumannAggregator<EvalT, Traits>::
NeumannAggregator(const Teuchos::ParameterList& p)
{
  Teuchos::RCP<PHX::DataLayout> dl =  p.get< Teuchos::RCP<PHX::DataLayout> >("Data Layout");

  const std::vector<std::string>& nbcs = *p.get<Teuchos::RCP<std::vector<std::string> > >("NBC Names");

  for (unsigned int i=0; i<nbcs.size(); i++) {
    PHX::Tag<ScalarT> fieldTag(nbcs[i], dl);
    this->addDependentField(fieldTag);
  }

  PHX::Tag<ScalarT> fieldTag(p.get<std::string>("NBC Aggregator Name"), dl);
  this->addEvaluatedField(fieldTag);

  this->setName("Neumann Aggregator" );
}

//**********************************************************************
}
