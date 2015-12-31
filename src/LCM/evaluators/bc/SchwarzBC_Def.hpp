//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#include "Albany_Application.hpp"
#include "Albany_GenericSTKMeshStruct.hpp"
#include "Albany_STKDiscretization.hpp"
#include "Intrepid2_MiniTensor.h"
#include "Phalanx_DataLayout.hpp"
#include "Sacado_ParameterRegistration.hpp"
#include "Teuchos_TestForException.hpp"

#if defined(ALBANY_DTK)
#include "Albany_OrdinarySTKFieldContainer.hpp"
#endif

//IKT, FIXME, 12/10/15: 
//SG and MP specializations are not implemented when ALBANY_DTK is ON. 
//This may never be needed... 

//#define DEBUG_LCM_SCHWARZ

//
// Generic Template Code for Constructor and PostRegistrationSetup
//

namespace LCM {

//
//
//
template<typename EvalT, typename Traits>
SchwarzBC_Base<EvalT, Traits>::
SchwarzBC_Base(Teuchos::ParameterList & p) :
    PHAL::DirichletBase<EvalT, Traits>(p),
    app_(p.get<Teuchos::RCP<Albany::Application>>(
        "Application", Teuchos::null)),
    coupled_apps_(app_->getApplications()),
    coupled_app_name_(p.get<std::string>("Coupled Application", "SELF")),
    coupled_block_name_(p.get<std::string>("Coupled Block", "NONE"))
{
  std::string const &
  nodeset_name = this->nodeSetID;

  app_->setCoupledAppBlockNodeset(
      coupled_app_name_,
      coupled_block_name_,
      nodeset_name);

  std::string const &
  this_app_name = app_->getAppName();

  auto const &
  app_name_index_map = *(app_->getAppNameIndexMap());

  auto
  it = app_name_index_map.find(this_app_name);

  assert(it != app_name_index_map.end());

  auto const
  this_app_index = it->second;

  setThisAppIndex(this_app_index);

  it = app_name_index_map.find(coupled_app_name_);

  assert(it != app_name_index_map.end());

  auto const
  coupled_app_index = it->second;

  setCoupledAppIndex(coupled_app_index);
}

//
//
//
template<typename EvalT, typename Traits>
void
SchwarzBC_Base<EvalT, Traits>::
computeBCs(
    size_t const ns_node,
    ScalarT & x_val,
    ScalarT & y_val,
    ScalarT & z_val)
{
  Teuchos::RCP<Teuchos::FancyOStream> 
  out = Teuchos::fancyOStream(Teuchos::VerboseObjectBase::getDefaultOStream());

  auto const
  this_app_index = getThisAppIndex();

  auto const
  coupled_app_index = getCoupledAppIndex();

  Albany::Application const &
  this_app = getApplication(this_app_index);

  Albany::Application const &
  coupled_app = getApplication(coupled_app_index);

  Teuchos::RCP<Albany::AbstractDiscretization>
  this_disc = this_app.getDiscretization();

  auto *
  this_stk_disc = static_cast<Albany::STKDiscretization *>(this_disc.get());

  Teuchos::RCP<Albany::AbstractDiscretization>
  coupled_disc = coupled_app.getDiscretization();

  auto *
  coupled_stk_disc =
      static_cast<Albany::STKDiscretization *>(coupled_disc.get());

  auto &
  coupled_gms = dynamic_cast<Albany::GenericSTKMeshStruct &>
      (*(coupled_stk_disc->getSTKMeshStruct()));

  auto const &
  coupled_ws_eb_names = coupled_disc->getWsEBNames();

  Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct>>
  coupled_mesh_specs = coupled_gms.getMeshSpecs();

  // Get cell topology of the application and block to which this node set
  // is coupled.
  std::string const &
  this_app_name = this_app.getAppName();

  std::string const &
  coupled_app_name = coupled_app.getAppName();

  std::string const
  coupled_block_name = this_app.getCoupledBlockName(coupled_app_index);

  bool const
  use_block = coupled_block_name != "NONE";

  std::map<std::string, int> const &
  coupled_block_name_2_index = coupled_gms.ebNameToIndex;

  auto
  it = coupled_block_name_2_index.find(coupled_block_name);

  bool const
  missing_block = it == coupled_block_name_2_index.end();

  if (use_block == true && missing_block == true) {
    std::cerr << "\nERROR: " << __PRETTY_FUNCTION__ << '\n';
    std::cerr << "Unknown coupled block: " << coupled_block_name << '\n';
    std::cerr << "Coupling application : " << this_app_name << '\n';
    std::cerr << "To application       : " << coupled_app_name << '\n';
    exit(1);
  }

  // When ignoring the block, set the index to zero to get defaults
  // corresponding to the first block.
  auto const
  coupled_block_index = use_block == true ? it->second : 0;

  CellTopologyData const
  coupled_cell_topology_data = coupled_mesh_specs[coupled_block_index]->ctd;

  shards::CellTopology
  coupled_cell_topology(&coupled_cell_topology_data);

  auto const
  coupled_dimension = coupled_cell_topology_data.dimension;

  // FIXME: Generalize element topology.
  auto const
  coupled_vertex_count = coupled_cell_topology_data.vertex_count;

  auto const
  coupled_element_type =
      Intrepid2::find_type(coupled_dimension, coupled_vertex_count);

  std::string const &
  coupled_nodeset_name = this_app.getNodesetName(coupled_app_index);

  std::vector<double *> const &
  ns_coord =
      this_stk_disc->getNodeSetCoords().find(coupled_nodeset_name)->second;

  auto const &
  ws_elem_2_node_id = coupled_stk_disc->getWsElNodeID();

  std::vector<Intrepid2::Vector<double>>
  coupled_element_vertices(coupled_vertex_count);

  std::vector<Intrepid2::Vector<double>>
  coupled_element_solution(coupled_vertex_count);

  for (auto i = 0; i < coupled_vertex_count; ++i) {
    coupled_element_vertices[i].set_dimension(coupled_dimension);
    coupled_element_solution[i].set_dimension(coupled_dimension);
  }

  // This tolerance is used for geometric approximations. It will be used
  // to determine whether a node of this_app is inside an element of
  // coupled_app within that tolerance.
  double const
  tolerance = 5.0e-2;

  double * const
  coord = ns_coord[ns_node];

  Intrepid2::Vector<double>
  point;

  point.set_dimension(coupled_dimension);

  point.fill(coord);

#if defined(DEBUG_LCM_SCHWARZ)
  std::cout << "--------------------------------------------------------\n";
  std::cout << "Current app      : " << this_app_name << '\n';
  std::cout << "Coupling to app  : " << coupled_app_name << '\n';
  std::cout << "Coupling to block: " << coupled_block_name << '\n';
  std::cout << "Node set node    : " << ns_node << '\n';
  std::cout << "Point            : " << point << '\n';
  std::cout << "--------------------------------------------------------\n";
#endif // DEBUG_LCM_SCHWARZ

  // Determine the element that contains this point.
  bool
  found = false;

  auto
  parametric_dimension = 0;

  Teuchos::RCP<Intrepid2::Basis<double, Intrepid2::FieldContainer<double>>>
  basis;

  Teuchos::ArrayRCP<double> const &
  coupled_coordinates = coupled_stk_disc->getCoordinates();

  Teuchos::RCP<Tpetra_Vector const>
  coupled_solution = coupled_stk_disc->getSolutionFieldT();

#if defined(DEBUG_LCM_SCHWARZ)
  if (ns_node == 0) { 
    *out << "coupled_solution: \n"; 
    coupled_solution->describe(*out, Teuchos::VERB_EXTREME);  
  }
#endif //DEBUG_LCM_SCHWARZ  
 
  Teuchos::ArrayRCP<ST const>
  coupled_solution_view = coupled_solution->get1dView();

  Teuchos::RCP<Tpetra_Map const>
  coupled_overlap_node_map = coupled_stk_disc->getOverlapNodeMapT();

  for (auto workset = 0; workset < ws_elem_2_node_id.size(); ++workset) {

    std::string const &
    coupled_element_block = coupled_ws_eb_names[workset];

    bool const
    block_names_differ = coupled_element_block != coupled_block_name;

    if (use_block == true && block_names_differ == true) continue;

    auto const
    elements_per_workset = ws_elem_2_node_id[workset].size();

    for (auto element = 0; element < elements_per_workset; ++element) {

      for (auto node = 0; node < coupled_vertex_count; ++node) {

        auto const
        global_node_id = ws_elem_2_node_id[workset][element][node];

        auto const
        local_node_id =
            coupled_overlap_node_map->getLocalElement(global_node_id);

        double * const
        pcoord = &(coupled_coordinates[coupled_dimension * local_node_id]);

        coupled_element_vertices[node].fill(pcoord);

        for (auto i = 0; i < coupled_dimension; ++i) {
          coupled_element_solution[node](i) =
              coupled_solution_view[coupled_dimension * local_node_id + i];
        } // dimension loop

      } // node loop

      bool
      in_element = false;

      switch (coupled_element_type) {

      default:
        std::cerr << "\nERROR: " << __PRETTY_FUNCTION__ << '\n';
        std::cerr << "Unknown element type: " << coupled_element_type << '\n';
        exit(1);
        break;

      case Intrepid2::ELEMENT::TETRAHEDRAL:
        parametric_dimension = 3;

        basis = Teuchos::rcp(new Intrepid2::Basis_HGRAD_TET_C1_FEM<
            double, Intrepid2::FieldContainer<double>>());

        in_element = Intrepid2::in_tetrahedron(
            point,
            coupled_element_vertices[0],
            coupled_element_vertices[1],
            coupled_element_vertices[2],
            coupled_element_vertices[3],
            tolerance);
        break;

      case Intrepid2::ELEMENT::HEXAHEDRAL:
        parametric_dimension = 3;

        basis = Teuchos::rcp(new Intrepid2::Basis_HGRAD_HEX_C1_FEM<
            double, Intrepid2::FieldContainer<double>>());

        in_element = Intrepid2::in_hexahedron(
            point,
            coupled_element_vertices[0],
            coupled_element_vertices[1],
            coupled_element_vertices[2],
            coupled_element_vertices[3],
            coupled_element_vertices[4],
            coupled_element_vertices[5],
            coupled_element_vertices[6],
            coupled_element_vertices[7],
            tolerance);
        break;

      } // switch

      if (in_element == true) {
        found = true;
        break;
      }

    } // element loop

    if (found == true) {
      break;
    }

  } // workset loop

  assert(found == true);

  // We do this element by element
  auto const
  number_cells = 1;

  // Container for the parametric coordinates
  Intrepid2::FieldContainer<double>
  parametric_point(number_cells, parametric_dimension);

  for (auto j = 0; j < parametric_dimension; ++j) {
    parametric_point(0, j) = 0.0;
  }

  // Container for the physical point
  Intrepid2::FieldContainer<double>
  physical_coordinates(number_cells, coupled_dimension);

  for (auto i = 0; i < coupled_dimension; ++i) {
    physical_coordinates(0, i) = point(i);
  }

  // Container for the physical nodal coordinates
  // TODO: matToReference more general, accepts more topologies.
  // Use it to find if point is contained in element as well.
  Intrepid2::FieldContainer<double>
  nodal_coordinates(number_cells, coupled_vertex_count, coupled_dimension);

  for (auto i = 0; i < coupled_vertex_count; ++i) {
    for (auto j = 0; j < coupled_dimension; ++j) {
      nodal_coordinates(0, i, j) = coupled_element_vertices[i](j);
    }
  }

  // Get parametric coordinates
  Intrepid2::CellTools<double>::mapToReferenceFrame(
      parametric_point,
      physical_coordinates,
      nodal_coordinates,
      coupled_cell_topology,
      0
      );

  // Evaluate shape functions at parametric point.
  auto const
  number_points = 1;

  Intrepid2::FieldContainer<double>
  basis_values(coupled_vertex_count, number_points);

  basis->getValues(basis_values, parametric_point, Intrepid2::OPERATOR_VALUE);

  // Evaluate solution at parametric point using values of shape
  // functions just computed.
  Intrepid2::Vector<double>
  value(coupled_dimension, Intrepid2::ZEROS);

#if defined(DEBUG_LCM_SCHWARZ)
  std::cout << "NODE   BASIS                     VALUE\n";
  std::cout << "---------------------------------------------------------\n";
#endif // DEBUG_LCM_SCHWARZ

  for (auto i = 0; i < coupled_vertex_count; ++i) {
    value += basis_values(i, 0) * coupled_element_solution[i];

#if defined(DEBUG_LCM_SCHWARZ)
    std::cout << std::setw(4) << i << ' ';
    std::cout << std::scientific << std::setw(24) << std::setprecision(16);
    std::cout << basis_values(i, 0) << "    ";
    std::cout << coupled_element_solution[i] << '\n';
#endif // DEBUG_LCM_SCHWARZ

  }

#if defined(DEBUG_LCM_SCHWARZ)
  std::cout << "--------------------------------------------------------\n";
  std::cout << "RESULT : " << value << '\n';
  std::cout << "--------------------------------------------------------\n";
#endif // DEBUG_LCM_SCHWARZ

  x_val = value(0);
  y_val = value(1);
  z_val = value(2);

  return;
}

//
//
//
#if defined(ALBANY_DTK)
template<typename EvalT, typename Traits>
Teuchos::RCP<Tpetra::MultiVector<double, int, DataTransferKit::SupportId>>
SchwarzBC_Base<EvalT, Traits>::
computeBCsDTK()
{
  Teuchos::RCP<Teuchos::FancyOStream> 
  out = Teuchos::fancyOStream(Teuchos::VerboseObjectBase::getDefaultOStream());

  Teuchos::RCP<Teuchos::FancyOStream> 
  outc = Teuchos::fancyOStream(Teuchos::rcpFromRef(std::cout));

#if defined(DEBUG_LCM_SCHWARZ)
  *out << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif //DEBUG_LCM_SCHWARZ
  auto const
  this_app_index = getThisAppIndex();

  auto const
  coupled_app_index = getCoupledAppIndex();

  Albany::Application const &
  this_app = getApplication(this_app_index);

  Albany::Application const &
  coupled_app = getApplication(coupled_app_index);

  //Check that neq is the same for this_app and coupled_app, which they should be
  assert(this_app.getNumEquations() == coupled_app.getNumEquations());  
  //Get number of equations from this_app 
  int neq = this_app.getNumEquations(); 

  //this_disc = target mesh
  Teuchos::RCP<Albany::AbstractDiscretization>
  this_disc = this_app.getDiscretization();

  auto *
  this_stk_disc = static_cast<Albany::STKDiscretization *>(this_disc.get());

  //coupled_disc = source mesh
  Teuchos::RCP<Albany::AbstractDiscretization>
  coupled_disc = coupled_app.getDiscretization();

  auto *
  coupled_stk_disc =
      static_cast<Albany::STKDiscretization *>(coupled_disc.get());

  //Source Mesh
  Teuchos::RCP<Albany::AbstractSTKMeshStruct> const
  coupled_stk_mesh_struct = coupled_stk_disc->getSTKMeshStruct();

  //get pointer to metadata from coupled_stk_disc
  Teuchos::RCP<stk::mesh::MetaData const> const
  coupled_meta_data = Teuchos::rcpFromRef(coupled_stk_disc->getSTKMetaData());

  //Get coupled_app parameter list 
  Teuchos::RCP<const Teuchos::ParameterList>
  coupled_app_params = coupled_app.getAppPL();

  //Get discretization sublist from coupled_app parameter list
  Teuchos::ParameterList
  dtk_params = coupled_app_params->sublist("DataTransferKit");
   
  //Get solution name from Discretization sublist
  std::string map_name =
  dtk_params.get("Map Type", "Consistent Interpolation");

#if defined(DEBUG_LCM_SCHWARZ)
  *out << "DEBUG: map_name: " << map_name << "\n";
#endif //DEBUG_LCM_SCHWARZ  

  using Field = stk::mesh::Field<double>;

  Albany::AbstractSTKFieldContainer::VectorFieldType*
  coupled_field = Teuchos::rcp_dynamic_cast<Albany::OrdinarySTKFieldContainer<true>>(
                  coupled_stk_disc->getSTKMeshStruct()->getFieldContainer())->getSolutionField();

  stk::mesh::Selector
  coupled_stk_selector =
      stk::mesh::Selector(coupled_meta_data->universal_part());

  Teuchos::RCP<stk::mesh::BulkData>
  coupled_bulk_data = Teuchos::rcpFromRef(coupled_field->get_mesh());

  //Target Mesh

  //get pointer to metadata from this_stk_disc
  Teuchos::RCP<stk::mesh::MetaData const>
  this_meta_data = Teuchos::rcpFromRef(this_stk_disc->getSTKMetaData());

  Albany::AbstractSTKFieldContainer::VectorFieldType*
  this_field = Teuchos::rcp_dynamic_cast<Albany::OrdinarySTKFieldContainer<true>>(
                  this_stk_disc->getSTKMeshStruct()->getFieldContainer())->getSolutionFieldDTK();

  // Get the part corresponding to this nodeset.
  std::string const &
  nodeset_name = this->nodeSetID;

  stk::mesh::Part *
  this_part = this_meta_data->get_part(nodeset_name);

  Teuchos::RCP<stk::mesh::BulkData>
  this_bulk_data = Teuchos::rcpFromRef(this_field->get_mesh());

  //Solution Transfer Setup

  // Create a manager for the source part elements.
  DataTransferKit::STKMeshManager
  coupled_manager(coupled_bulk_data, coupled_stk_selector);

  // Create a manager for the target part nodes.
  stk::mesh::Selector
  this_stk_selector(*this_part);

  DataTransferKit::STKMeshManager
  this_manager(this_bulk_data, this_stk_selector);

  // Create a solution vector for the source.
  Teuchos::RCP<Tpetra::MultiVector<double, int, DataTransferKit::SupportId>>
  coupled_vector =
      coupled_manager.createFieldMultiVector<Albany::AbstractSTKFieldContainer::VectorFieldType>(
          Teuchos::ptr(coupled_field), neq);

  // Create a solution vector for the target.
  Teuchos::RCP<Tpetra::MultiVector<double, int, DataTransferKit::SupportId>>
  this_vector = this_manager.createFieldMultiVector<Albany::AbstractSTKFieldContainer::VectorFieldType>(
      Teuchos::ptr(this_field), neq);

#if defined(DEBUG_LCM_SCHWARZ)
  // Print out source mesh info.
  Teuchos::RCP<Teuchos::Describable>
  coupled_describe = coupled_manager.functionSpace()->entitySet();
  std::cout << "Source Mesh \n";
  coupled_describe->describe(std::cout);
  std::cout << "\n";

  // Print out target mesh info.
  Teuchos::RCP<Teuchos::Describable>
  this_describe = this_manager.functionSpace()->entitySet();
  std::cout << "Target Mesh \n";
  this_describe->describe(std::cout);
  std::cout << "\n";
#endif //DEBUG_LCM_SCHWARZ  

  //Solution transfer

  DataTransferKit::MapOperatorFactory
  op_factory;

  Teuchos::RCP<DataTransferKit::MapOperator> map_op =
      op_factory.create(coupled_vector->getMap(),
          this_vector->getMap(),
          dtk_params);

  // Setup the map operator. This creates the underlying linear operators.
  map_op->setup(coupled_manager.functionSpace(), this_manager.functionSpace());

  // Apply the map operator. This interpolates the data from one STK field
  // to the other.
  map_op->apply(*coupled_vector, *this_vector);

#if defined(DEBUG_LCM_SCHWARZ)
  *out << "coupled_vector: \n ";
  coupled_vector->describe(*outc, Teuchos::VERB_EXTREME);
  *out << "this_vector: \n ";
  this_vector->describe(*outc, Teuchos::VERB_EXTREME);
#endif //DEBUG_LCM_SCHWARZ  

  return this_vector;
}
#endif //ALBANY_DTK

//
// Specialization: Residual
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::Residual, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
    SchwarzBC_Base<PHAL::AlbanyTraits::Residual, Traits>(p)
{
}

//
//
//
template<typename Traits>
void
SchwarzBC<PHAL::AlbanyTraits::Residual, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Teuchos::FancyOStream> 
  out = Teuchos::fancyOStream(Teuchos::VerboseObjectBase::getDefaultOStream());
  
  Teuchos::RCP<Teuchos::FancyOStream> 
  outc = Teuchos::fancyOStream(Teuchos::rcpFromRef(std::cout));

  // Solution
  Teuchos::RCP<const Tpetra_Vector>
  xT = dirichlet_workset.xT;

  Teuchos::ArrayRCP<const ST>
  xT_const_view = xT->get1dView();

  // Residual
  Teuchos::RCP<Tpetra_Vector>
  fT = dirichlet_workset.fT;

  Teuchos::ArrayRCP<ST>
  fT_view = fT->get1dViewNonConst();

  std::vector<std::vector<int>> const &
  ns_dof = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  auto const
  ns_number_nodes = ns_dof.size();

#if defined(ALBANY_DTK)
#if defined(DEBUG_LCM_SCHWARZ)
  *out << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif //DEBUG_LCM_SCHWARZ  

  Teuchos::RCP<Tpetra::MultiVector<double, int, DataTransferKit::SupportId>> const
  schwarz_bcs = this->computeBCsDTK();
  
  Teuchos::RCP<const Teuchos::Comm<int> > 
  commT = schwarz_bcs->getMap()->getComm(); 
  
  Teuchos::ArrayRCP<const ST> 
  schwarz_bcs_const_view_x = schwarz_bcs->getData(0); 

  Teuchos::ArrayRCP<const ST> 
  schwarz_bcs_const_view_y = schwarz_bcs->getData(1); 

  Teuchos::ArrayRCP<const ST> 
  schwarz_bcs_const_view_z = schwarz_bcs->getData(2); 

  for (auto ns_node = 0; ns_node < ns_number_nodes; ++ns_node) {

    auto const
    x_dof = ns_dof[ns_node][0];

    auto const
    y_dof = ns_dof[ns_node][1];

    auto const
    z_dof = ns_dof[ns_node][2];

    int dof = x_dof/3; 
    
#if defined(DEBUG_LCM_SCHWARZ)
    std::cout << "proc#, ns_node, x_dof, y_dof, z_dof, dof, x_val, y_val, z_val: " 
              << commT->getRank() << ", " << ns_node <<  ", " 
              << x_dof << ", " << y_dof << ", " << z_dof << ", " << dof << ", " << schwarz_bcs_const_view_x[dof] 
              << ", " << schwarz_bcs_const_view_y[dof] << ", " << schwarz_bcs_const_view_z[dof] << "\n";
#endif //DEBUG_LCM_SCHWARZ  

    fT_view[x_dof] = xT_const_view[x_dof] - schwarz_bcs_const_view_x[dof];
    fT_view[y_dof] = xT_const_view[y_dof] - schwarz_bcs_const_view_y[dof];
    fT_view[z_dof] = xT_const_view[z_dof] - schwarz_bcs_const_view_z[dof];

  } 
#else
  for (auto ns_node = 0; ns_node < ns_number_nodes; ++ns_node) {

    ScalarT
    x_val, y_val, z_val;

    this->computeBCs(ns_node, x_val, y_val, z_val);

    auto const
    x_dof = ns_dof[ns_node][0];

    auto const
    y_dof = ns_dof[ns_node][1];

    auto const
    z_dof = ns_dof[ns_node][2];

#if defined(DEBUG_LCM_SCHWARZ)
    std::cout << "ns_node, x_dof, y_dof, z_dof, x_val, y_val, z_val: "  << ns_node <<  ", " 
              << x_dof << ", " << y_dof << ", " << z_dof <<  ", " << x_val 
              << ", " << y_val << ", " << z_val << "\n";
#endif //DEBUG_LCM_SCHWARZ  

    fT_view[x_dof] = xT_const_view[x_dof] - x_val;
    fT_view[y_dof] = xT_const_view[y_dof] - y_val;
    fT_view[z_dof] = xT_const_view[z_dof] - z_val;

  } // node in node set loop
#endif //ALBANY_DTK
#if defined(DEBUG_LCM_SCHWARZ)
  *out << "fT: \n ";
  fT->describe(*outc, Teuchos::VERB_EXTREME);
#endif //DEBUG_LCM_SCHWARZ  
  return;
}

//
// Specialization: Jacobian
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::Jacobian, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
    SchwarzBC_Base<PHAL::AlbanyTraits::Jacobian, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::Jacobian, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Teuchos::FancyOStream> 
  out = Teuchos::fancyOStream(Teuchos::VerboseObjectBase::getDefaultOStream());

  Teuchos::RCP<Teuchos::FancyOStream> 
  outc = Teuchos::fancyOStream(Teuchos::rcpFromRef(std::cout));

  Teuchos::RCP<Tpetra_Vector>
  fT = dirichlet_workset.fT;

  Teuchos::ArrayRCP<ST>
  fT_view;

  Teuchos::RCP<Tpetra_CrsMatrix>
  jacT = dirichlet_workset.JacT;

  Teuchos::RCP<const Tpetra_Vector>
  xT = dirichlet_workset.xT;

  Teuchos::ArrayRCP<const ST>
  xT_const_view = xT->get1dView();

  RealType const
  j_coeff = dirichlet_workset.j_coeff;

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  Teuchos::Array<LO>
  index(1);

  Teuchos::Array<ST>
  value(1);

  value[0] = j_coeff;

  Teuchos::Array<ST>
  matrix_entries;

  Teuchos::Array<LO>
  matrix_indices;

  bool const
  fill_residual = (fT != Teuchos::null);

  if (fill_residual == true) {
    fT_view = fT->get1dViewNonConst();
  }

  for (auto ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {

    auto const
    x_dof = ns_nodes[ns_node][0];

    auto const
    y_dof = ns_nodes[ns_node][1];

    auto const
    z_dof = ns_nodes[ns_node][2];

    // replace jac values for the X dof
    auto
    num_entries = jacT->getNumEntriesInLocalRow(x_dof);

    matrix_entries.resize(num_entries);
    matrix_indices.resize(num_entries);

    jacT->getLocalRowCopy(
        x_dof,
        matrix_indices(),
        matrix_entries(),
        num_entries);

    for (auto i = 0; i < num_entries; ++i) {
      matrix_entries[i] = 0;
    }

    jacT->replaceLocalValues(x_dof, matrix_indices(), matrix_entries());
    index[0] = x_dof;
    jacT->replaceLocalValues(x_dof, index(), value());

    // replace jac values for the y dof
    num_entries = jacT->getNumEntriesInLocalRow(y_dof);

    matrix_entries.resize(num_entries);
    matrix_indices.resize(num_entries);

    jacT->getLocalRowCopy(
        y_dof,
        matrix_indices(),
        matrix_entries(),
        num_entries);

    for (auto i = 0; i < num_entries; ++i) {
      matrix_entries[i] = 0;
    }

    jacT->replaceLocalValues(y_dof, matrix_indices(), matrix_entries());
    index[0] = y_dof;
    jacT->replaceLocalValues(y_dof, index(), value());

    // replace jac values for the z dof
    num_entries = jacT->getNumEntriesInLocalRow(z_dof);

    matrix_entries.resize(num_entries);
    matrix_indices.resize(num_entries);

    jacT->getLocalRowCopy(
        z_dof,
        matrix_indices(),
        matrix_entries(),
        num_entries);

    for (auto i = 0; i < num_entries; ++i) {
      matrix_entries[i] = 0;
    }

    jacT->replaceLocalValues(z_dof, matrix_indices(), matrix_entries());
    index[0] = z_dof;
    jacT->replaceLocalValues(z_dof, index(), value());
  }

  if (fill_residual == true) {

#if defined(ALBANY_DTK)
#if defined(DEBUG_LCM_SCHWARZ)
    *out << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif //DEBUG_LCM_SCHWARZ  
    Teuchos::RCP<Tpetra::MultiVector<double, int, DataTransferKit::SupportId>> const
    
    schwarz_bcs = this->computeBCsDTK();
     
    Teuchos::RCP<const Teuchos::Comm<int> > 
    commT = schwarz_bcs->getMap()->getComm(); 
  
    Teuchos::ArrayRCP<const ST> 
    schwarz_bcs_const_view_x = schwarz_bcs->getData(0); 

    Teuchos::ArrayRCP<const ST> 
    schwarz_bcs_const_view_y = schwarz_bcs->getData(1); 

    Teuchos::ArrayRCP<const ST> 
    schwarz_bcs_const_view_z = schwarz_bcs->getData(2); 

    for (auto ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {

      auto const
      x_dof = ns_nodes[ns_node][0];

      auto const
      y_dof = ns_nodes[ns_node][1];

      auto const
      z_dof = ns_nodes[ns_node][2];

      int dof = x_dof/3; 
    
#if defined(DEBUG_LCM_SCHWARZ)
      std::cout << "proc#, ns_node, x_dof, y_dof, z_dof, dof, x_val, y_val, z_val: " 
                << commT->getRank() << ", " << ns_node <<  ", " 
                << x_dof << ", " << y_dof << ", " << z_dof << ", " << dof << ", " << schwarz_bcs_const_view_x[dof] 
                << ", " << schwarz_bcs_const_view_y[dof] << ", " << schwarz_bcs_const_view_z[dof] << "\n";
#endif //DEBUG_LCM_SCHWARZ  

      fT_view[x_dof] = xT_const_view[x_dof] - schwarz_bcs_const_view_x[dof];
      fT_view[y_dof] = xT_const_view[y_dof] - schwarz_bcs_const_view_y[dof];
      fT_view[z_dof] = xT_const_view[z_dof] - schwarz_bcs_const_view_z[dof];
    } 
#else    
    for (auto ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {
    
      auto const
      x_dof = ns_nodes[ns_node][0];

      auto const
      y_dof = ns_nodes[ns_node][1];

      auto const
      z_dof = ns_nodes[ns_node][2];

      ScalarT
      x_val, y_val, z_val;

      this->computeBCs(ns_node, x_val, y_val, z_val);

      fT_view[x_dof] = xT_const_view[x_dof] - x_val.val();
      fT_view[y_dof] = xT_const_view[y_dof] - y_val.val();
      fT_view[z_dof] = xT_const_view[z_dof] - z_val.val();
    }
#endif //ALBANY_DTK
  }
#if defined(DEBUG_LCM_SCHWARZ)
  if (fill_residual == true) {
    *out << "fT: \n ";
    fT->describe(*outc, Teuchos::VERB_EXTREME);
  }
#endif //DEBUG_LCM_SCHWARZ  
}

//
// Specialization: Tangent
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::Tangent, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
    SchwarzBC_Base<PHAL::AlbanyTraits::Tangent, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::Tangent, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Teuchos::FancyOStream> 
  out = Teuchos::fancyOStream(Teuchos::VerboseObjectBase::getDefaultOStream());
  
  Teuchos::RCP<Teuchos::FancyOStream> 
  outc = Teuchos::fancyOStream(Teuchos::rcpFromRef(std::cout));

  Teuchos::RCP<Tpetra_Vector>
  fT = dirichlet_workset.fT;

  Teuchos::RCP<Tpetra_MultiVector>
  fpT = dirichlet_workset.fpT;

  Teuchos::RCP<Tpetra_MultiVector>
  JVT = dirichlet_workset.JVT;

  Teuchos::RCP<const Tpetra_Vector>
  xT = dirichlet_workset.xT;

  Teuchos::RCP<const Tpetra_MultiVector>
  VxT = dirichlet_workset.VxT;

  RealType const
  j_coeff = dirichlet_workset.j_coeff;

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  Teuchos::ArrayRCP<const ST>
  VxT_const_view;

  Teuchos::ArrayRCP<ST>
  fT_view;

  Teuchos::ArrayRCP<const ST>
  xT_const_view = xT->get1dView();

  if (fT != Teuchos::null) {
    fT_view = fT->get1dViewNonConst();
  }

  for (auto ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {
   
    auto const
    x_dof = ns_nodes[ns_node][0];

    auto const
    y_dof = ns_nodes[ns_node][1];

    auto const
    z_dof = ns_nodes[ns_node][2];

    if (JVT != Teuchos::null) {
      Teuchos::ArrayRCP<ST>
      JVT_view;

      for (auto i = 0; i < dirichlet_workset.num_cols_x; ++i) {
        JVT_view = JVT->getDataNonConst(i);
        VxT_const_view = VxT->getData(i);
        JVT_view[x_dof] = j_coeff * VxT_const_view[x_dof];
        JVT_view[y_dof] = j_coeff * VxT_const_view[y_dof];
        JVT_view[z_dof] = j_coeff * VxT_const_view[z_dof];
      }
    }
  }

  if (fT != Teuchos::null || fpT != Teuchos::null) {

#if defined(ALBANY_DTK)
#if defined(DEBUG_LCM_SCHWARZ)
  *out << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif //DEBUG_LCM_SCHWARZ  
    if (fT != Teuchos::null) {

      Teuchos::RCP<Tpetra::MultiVector<double, int, DataTransferKit::SupportId>> const
      schwarz_bcs = this->computeBCsDTK();
    
      Teuchos::RCP<const Teuchos::Comm<int> > 
      commT = schwarz_bcs->getMap()->getComm(); 

      Teuchos::ArrayRCP<const ST> 
      schwarz_bcs_const_view_x = schwarz_bcs->getData(0); 

      Teuchos::ArrayRCP<const ST> 
      schwarz_bcs_const_view_y = schwarz_bcs->getData(1); 

      Teuchos::ArrayRCP<const ST> 
      schwarz_bcs_const_view_z = schwarz_bcs->getData(2); 

      for (auto ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {

        auto const
        x_dof = ns_nodes[ns_node][0];

        auto const
        y_dof = ns_nodes[ns_node][1];

        auto const
        z_dof = ns_nodes[ns_node][2];

        int dof = x_dof/3; 
    
#if defined(DEBUG_LCM_SCHWARZ)
        std::cout << "proc#, ns_node, x_dof, y_dof, z_dof, dof, x_val, y_val, z_val: " 
                  << commT->getRank() << ", " << ns_node <<  ", " 
                  << x_dof << ", " << y_dof << ", " << z_dof << ", " << dof << ", " << schwarz_bcs_const_view_x[dof] 
                  << ", " << schwarz_bcs_const_view_y[dof] << ", " << schwarz_bcs_const_view_z[dof] << "\n";
#endif //DEBUG_LCM_SCHWARZ  

        fT_view[x_dof] = xT_const_view[x_dof] - schwarz_bcs_const_view_x[dof];
        fT_view[y_dof] = xT_const_view[y_dof] - schwarz_bcs_const_view_y[dof];
        fT_view[z_dof] = xT_const_view[z_dof] - schwarz_bcs_const_view_z[dof];
      }
    } 

    if (fpT != Teuchos::null) {
      std::cout << "WARNING: fpT requested by not set yet when ALBANY_DTK is ON! \n"; 
    }
#else  
    for (auto ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {

      auto const
      x_dof = ns_nodes[ns_node][0];

      auto const
      y_dof = ns_nodes[ns_node][1];

      auto const
      z_dof = ns_nodes[ns_node][2];

      ScalarT
      x_val, y_val, z_val;

      this->computeBCs(ns_node, x_val, y_val, z_val);
      
      if (fT != Teuchos::null) {
        fT_view[x_dof] = xT_const_view[x_dof] - x_val.val();
        fT_view[y_dof] = xT_const_view[y_dof] - y_val.val();
        fT_view[z_dof] = xT_const_view[z_dof] - z_val.val();
      }
      if (fpT != Teuchos::null) {
        Teuchos::ArrayRCP<ST>
        fpT_view;

        for (auto i = 0; i < dirichlet_workset.num_cols_p; ++i) {
          fpT_view = fpT->getDataNonConst(i);
          fpT_view[x_dof] = -x_val.dx(dirichlet_workset.param_offset + i);
          fpT_view[y_dof] = -y_val.dx(dirichlet_workset.param_offset + i);
          fpT_view[z_dof] = -z_val.dx(dirichlet_workset.param_offset + i);
        }
      }
    }
#endif //ALBANY_DTK
  }
#if defined(DEBUG_LCM_SCHWARZ)
  if (fT != Teuchos::null) {
    *out << "fT: \n ";
    fT->describe(*outc, Teuchos::VERB_EXTREME);
  }
#endif //DEBUG_LCM_SCHWARZ  
  return;
}

//
// Specialization: DistParamDeriv
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::DistParamDeriv, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
    SchwarzBC_Base<PHAL::AlbanyTraits::DistParamDeriv, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::DistParamDeriv, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Tpetra_MultiVector>
  fpVT = dirichlet_workset.fpVT;

  Teuchos::ArrayRCP<ST>
  fpVT_view;

  bool const
  trans = dirichlet_workset.transpose_dist_param_deriv;

  auto const
  num_cols = fpVT->getNumVectors();

  //
  // We're currently assuming Dirichlet BC's can't be distributed parameters.
  // Thus we don't need to actually evaluate the BC's here.  The code to do
  // so is still here, just commented out for future reference.
  //

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  std::vector<double *> const &
  ns_coord = dirichlet_workset.nodeSetCoords->find(this->nodeSetID)->second;

  // For (df/dp)^T*V we zero out corresponding entries in V
  if (trans == true) {
    Teuchos::RCP<Tpetra_MultiVector>
    VpT = dirichlet_workset.Vp_bcT;

    Teuchos::ArrayRCP<ST>
    VpT_view;

    for (auto inode = 0; inode < ns_nodes.size(); ++inode) {

      auto const
      x_dof = ns_nodes[inode][0];

      auto const
      y_dof = ns_nodes[inode][1];

      auto const
      z_dof = ns_nodes[inode][2];

      for (auto col = 0; col < num_cols; ++col) {

        VpT_view = VpT->getDataNonConst(col);
        VpT_view[x_dof] = 0.0;
        VpT_view[y_dof] = 0.0;
        VpT_view[z_dof] = 0.0;
      }
    }
  }

  // for (df/dp)*V we zero out corresponding entries in df/dp
  else {
    for (auto inode = 0; inode < ns_nodes.size(); ++inode) {

      auto const
      x_dof = ns_nodes[inode][0];

      auto const
      y_dof = ns_nodes[inode][1];

      auto const
      z_dof = ns_nodes[inode][2];

      for (auto col = 0; col < num_cols; ++col) {

        fpVT_view = fpVT->getDataNonConst(col);
        fpVT_view[x_dof] = 0.0;
        fpVT_view[y_dof] = 0.0;
        fpVT_view[z_dof] = 0.0;
      }
    }
  }
}

//
// Specialization: Stochastic Galerkin Residual
//
#ifdef ALBANY_SG
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::SGResidual, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
SchwarzBC_Base<PHAL::AlbanyTraits::SGResidual, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::SGResidual, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Stokhos::EpetraVectorOrthogPoly>
  f = dirichlet_workset.sg_f;

  Teuchos::RCP<const Stokhos::EpetraVectorOrthogPoly>
  x = dirichlet_workset.sg_x;

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  std::vector<double*> const &
  ns_coord = dirichlet_workset.nodeSetCoords->find(this->nodeSetID)->second;

  // global and local indices into unknown vector
  int
  x_dof, y_dof, z_dof;

  double *
  coord;

  ScalarT
  x_val, y_val, z_val;

  int const
  nblock = x->size();

  for (size_t ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {
    x_dof = ns_nodes[ns_node][0];
    y_dof = ns_nodes[ns_node][1];
    z_dof = ns_nodes[ns_node][2];
    coord = ns_coord[ns_node];

    this->computeBCs(ns_node, x_val, y_val, z_val);

    for (int block = 0; block < nblock; ++block) {
      (*f)[block][x_dof] = (*x)[block][x_dof] - x_val.coeff(block);
      (*f)[block][y_dof] = (*x)[block][y_dof] - y_val.coeff(block);
      (*f)[block][z_dof] = (*x)[block][z_dof] - z_val.coeff(block);
    }
  }
}

//
// Specialization: Stochastic Galerkin Jacobian
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::SGJacobian, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
SchwarzBC_Base<PHAL::AlbanyTraits::SGJacobian, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::SGJacobian, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP< Stokhos::EpetraVectorOrthogPoly>
  f = dirichlet_workset.sg_f;

  Teuchos::RCP< Stokhos::VectorOrthogPoly<Epetra_CrsMatrix>>
  jac = dirichlet_workset.sg_Jac;

  Teuchos::RCP<const Stokhos::EpetraVectorOrthogPoly>
  x = dirichlet_workset.sg_x;

  RealType const
  j_coeff = dirichlet_workset.j_coeff;

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  std::vector<double*> const &
  ns_coord = dirichlet_workset.nodeSetCoords->find(this->nodeSetID)->second;

  RealType *
  matrix_entries;

  int *
  matrix_indices;

  int
  num_entries;

  RealType
  diag = j_coeff;

  bool
  fill_residual = (f != Teuchos::null);

  int
  nblock = 0;

  if (f != Teuchos::null) {
    nblock = f->size();
  }

  int const
  nblock_jac = jac->size();

  // local indices into unknown vector
  int
  x_dof, y_dof, z_dof;

  double *
  coord;

  ScalarT
  x_val, y_val, z_val;

  for (size_t ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {
    x_dof = ns_nodes[ns_node][0];
    y_dof = ns_nodes[ns_node][1];
    z_dof = ns_nodes[ns_node][2];
    coord = ns_coord[ns_node];

    this->computeBCs(ns_node, x_val, y_val, z_val);

    // replace jac values for the X dof
    for (int block = 0; block < nblock_jac; ++block) {
      (*jac)[block].ExtractMyRowView(x_dof, num_entries, matrix_entries,
          matrix_indices);
      for (int i = 0; i < num_entries; ++i) matrix_entries[i] = 0;

      // replace jac values for the y dof
      (*jac)[block].ExtractMyRowView(y_dof, num_entries, matrix_entries,
          matrix_indices);
      for (int i = 0; i < num_entries; ++i) matrix_entries[i] = 0;

      // replace jac values for the z dof
      (*jac)[block].ExtractMyRowView(z_dof, num_entries, matrix_entries,
          matrix_indices);
      for (int i = 0; i < num_entries; ++i) matrix_entries[i] = 0;
    }

    (*jac)[0].ReplaceMyValues(x_dof, 1, &diag, &x_dof);
    (*jac)[0].ReplaceMyValues(y_dof, 1, &diag, &y_dof);
    (*jac)[0].ReplaceMyValues(z_dof, 1, &diag, &z_dof);

    if (fill_residual == true) {

      for (int block = 0; block < nblock; ++block) {
        (*f)[block][x_dof] = (*x)[block][x_dof] - x_val.val().coeff(block);
        (*f)[block][y_dof] = (*x)[block][y_dof] - y_val.val().coeff(block);
        (*f)[block][z_dof] = (*x)[block][z_dof] - z_val.val().coeff(block);
      }
    }
  }
}

//
// Specialization: Stochastic Galerkin Tangent
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::SGTangent, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
SchwarzBC_Base<PHAL::AlbanyTraits::SGTangent, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::SGTangent, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Stokhos::EpetraVectorOrthogPoly>
  f = dirichlet_workset.sg_f;

  Teuchos::RCP<Stokhos::EpetraMultiVectorOrthogPoly>
  fp = dirichlet_workset.sg_fp;

  Teuchos::RCP<Stokhos::EpetraMultiVectorOrthogPoly>
  JV = dirichlet_workset.sg_JV;

  Teuchos::RCP<const Stokhos::EpetraVectorOrthogPoly>
  x = dirichlet_workset.sg_x;

  Teuchos::RCP<Epetra_MultiVector const>
  Vx = dirichlet_workset.Vx;

  RealType const
  j_coeff = dirichlet_workset.j_coeff;

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  std::vector<double*> const &
  ns_coord = dirichlet_workset.nodeSetCoords->find(this->nodeSetID)->second;

  int const
  nblock = x->size();

  // global and local indices into unknown vector
  int
  x_dof, y_dof, z_dof;

  double *
  coord;

  ScalarT
  x_val, y_val, z_val;

  for (size_t ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {
    x_dof = ns_nodes[ns_node][0];
    y_dof = ns_nodes[ns_node][1];
    z_dof = ns_nodes[ns_node][2];
    coord = ns_coord[ns_node];

    this->computeBCs(ns_node, x_val, y_val, z_val);

    if (f != Teuchos::null) {

      for (int block = 0; block < nblock; ++block) {
        (*f)[block][x_dof] = (*x)[block][x_dof] - x_val.val().coeff(block);
        (*f)[block][y_dof] = (*x)[block][y_dof] - y_val.val().coeff(block);
        (*f)[block][z_dof] = (*x)[block][z_dof] - z_val.val().coeff(block);
      }
    }

    if (JV != Teuchos::null) {
      for (int i = 0; i < dirichlet_workset.num_cols_x; ++i) {
        (*JV)[0][i][x_dof] = j_coeff*(*Vx)[i][x_dof];
        (*JV)[0][i][y_dof] = j_coeff*(*Vx)[i][y_dof];
        (*JV)[0][i][z_dof] = j_coeff*(*Vx)[i][z_dof];
      }
    }

    if (fp != Teuchos::null) {
      for (int i=0; i<dirichlet_workset.num_cols_p; ++i) {
        for (int block = 0; block < nblock; ++block) {
          (*fp)[block][i][x_dof] =
          -x_val.dx(dirichlet_workset.param_offset+i).coeff(block);
          (*fp)[block][i][y_dof] =
          -y_val.dx(dirichlet_workset.param_offset+i).coeff(block);
          (*fp)[block][i][z_dof] =
          -z_val.dx(dirichlet_workset.param_offset+i).coeff(block);
        }
      }
    }

  }
}
#endif 
#ifdef ALBANY_ENSEMBLE 

//
// Specialization: Multi-point Residual
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::MPResidual, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
SchwarzBC_Base<PHAL::AlbanyTraits::MPResidual, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::MPResidual, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Stokhos::ProductEpetraVector>
  f = dirichlet_workset.mp_f;

  Teuchos::RCP<Stokhos::ProductEpetraVector const>
  x = dirichlet_workset.mp_x;

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  std::vector<double*> const &
  ns_coord = dirichlet_workset.nodeSetCoords->find(this->nodeSetID)->second;

  // global and local indices into unknown vector
  int
  x_dof, y_dof, z_dof;

  double *
  coord;

  ScalarT
  x_val, y_val, z_val;

  int const
  nblock = x->size();

  for (size_t ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {
    x_dof = ns_nodes[ns_node][0];
    y_dof = ns_nodes[ns_node][1];
    z_dof = ns_nodes[ns_node][2];
    coord = ns_coord[ns_node];

    this->computeBCs(ns_node, x_val, y_val, z_val);

    for (int block = 0; block < nblock; ++block) {
      (*f)[block][x_dof] = (*x)[block][x_dof] - x_val.coeff(block);
      (*f)[block][y_dof] = (*x)[block][y_dof] - y_val.coeff(block);
      (*f)[block][z_dof] = (*x)[block][z_dof] - z_val.coeff(block);
    }
  }
}

//
// Specialization: Multi-point Jacobian
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::MPJacobian, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
SchwarzBC_Base<PHAL::AlbanyTraits::MPJacobian, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::MPJacobian, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Stokhos::ProductEpetraVector>
  f = dirichlet_workset.mp_f;

  Teuchos::RCP< Stokhos::ProductContainer<Epetra_CrsMatrix>>
  jac = dirichlet_workset.mp_Jac;

  Teuchos::RCP<Stokhos::ProductEpetraVector const>
  x = dirichlet_workset.mp_x;

  RealType const
  j_coeff = dirichlet_workset.j_coeff;

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  std::vector<double*> const &
  ns_coord = dirichlet_workset.nodeSetCoords->find(this->nodeSetID)->second;

  RealType *
  matrix_entries;

  int *
  matrix_indices;

  int
  num_entries;

  RealType
  diag = j_coeff;

  bool
  fill_residual = (f != Teuchos::null);

  int
  nblock = 0;

  if (f != Teuchos::null) {
    nblock = f->size();
  }

  int const
  nblock_jac = jac->size();

  // local indices into unknown vector
  int
  x_dof, y_dof, z_dof;

  double *
  coord;

  ScalarT
  x_val, y_val, z_val;

  for (size_t ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {
    x_dof = ns_nodes[ns_node][0];
    y_dof = ns_nodes[ns_node][1];
    z_dof = ns_nodes[ns_node][2];
    coord = ns_coord[ns_node];

    this->computeBCs(ns_node, x_val, y_val, z_val);

    // replace jac values for the X dof
    for (int block=0; block<nblock_jac; ++block) {
      (*jac)[block].ExtractMyRowView(x_dof, num_entries, matrix_entries,
          matrix_indices);
      for (int i = 0; i < num_entries; ++i) matrix_entries[i] = 0;
      (*jac)[block].ReplaceMyValues(x_dof, 1, &diag, &x_dof);

      // replace jac values for the y dof
      (*jac)[block].ExtractMyRowView(y_dof, num_entries, matrix_entries,
          matrix_indices);
      for (int i = 0; i < num_entries; ++i) matrix_entries[i] = 0;
      (*jac)[block].ReplaceMyValues(y_dof, 1, &diag, &y_dof);

      // replace jac values for the z dof
      (*jac)[block].ExtractMyRowView(z_dof, num_entries, matrix_entries,
          matrix_indices);
      for (int i = 0; i < num_entries; ++i) matrix_entries[i] = 0;
      (*jac)[block].ReplaceMyValues(z_dof, 1, &diag, &z_dof);
    }

    if (fill_residual == true) {

      for (int block = 0; block < nblock; ++block) {
        (*f)[block][x_dof] = (*x)[block][x_dof] - x_val.val().coeff(block);
        (*f)[block][y_dof] = (*x)[block][y_dof] - y_val.val().coeff(block);
        (*f)[block][z_dof] = (*x)[block][z_dof] - z_val.val().coeff(block);
      }
    }
  }
}

//
// Specialization: Multi-point Tangent
//
template<typename Traits>
SchwarzBC<PHAL::AlbanyTraits::MPTangent, Traits>::
SchwarzBC(Teuchos::ParameterList & p) :
SchwarzBC_Base<PHAL::AlbanyTraits::MPTangent, Traits>(p)
{
}

//
//
//
template<typename Traits>
void SchwarzBC<PHAL::AlbanyTraits::MPTangent, Traits>::
evaluateFields(typename Traits::EvalData dirichlet_workset)
{
  Teuchos::RCP<Stokhos::ProductEpetraVector>
  f = dirichlet_workset.mp_f;

  Teuchos::RCP<Stokhos::ProductEpetraMultiVector>
  fp = dirichlet_workset.mp_fp;

  Teuchos::RCP<Stokhos::ProductEpetraMultiVector>
  JV = dirichlet_workset.mp_JV;

  Teuchos::RCP<Stokhos::ProductEpetraVector const>
  x = dirichlet_workset.mp_x;

  Teuchos::RCP<Epetra_MultiVector const>
  Vx = dirichlet_workset.Vx;

  RealType const
  j_coeff = dirichlet_workset.j_coeff;

  std::vector<std::vector<int>> const &
  ns_nodes = dirichlet_workset.nodeSets->find(this->nodeSetID)->second;

  std::vector<double*> const &
  ns_coord = dirichlet_workset.nodeSetCoords->find(this->nodeSetID)->second;

  int const
  nblock = x->size();

  // global and local indices into unknown vector
  int
  x_dof, y_dof, z_dof;

  double *
  coord;

  ScalarT
  x_val, y_val, z_val;

  for (size_t ns_node = 0; ns_node < ns_nodes.size(); ++ns_node) {
    x_dof = ns_nodes[ns_node][0];
    y_dof = ns_nodes[ns_node][1];
    z_dof = ns_nodes[ns_node][2];
    coord = ns_coord[ns_node];

    this->computeBCs(ns_node, x_val, y_val, z_val);

    if (f != Teuchos::null) {

      for (int block = 0; block < nblock; ++block) {
        (*f)[block][x_dof] = (*x)[block][x_dof] - x_val.val().coeff(block);
        (*f)[block][y_dof] = (*x)[block][y_dof] - y_val.val().coeff(block);
        (*f)[block][z_dof] = (*x)[block][z_dof] - z_val.val().coeff(block);
      }
    }

    if (JV != Teuchos::null) {
      for (int i = 0; i<dirichlet_workset.num_cols_x; ++i) {
        for (int block = 0; block < nblock; ++block) {
          (*JV)[block][i][x_dof] = j_coeff*(*Vx)[i][x_dof];
          (*JV)[block][i][y_dof] = j_coeff*(*Vx)[i][y_dof];
          (*JV)[block][i][z_dof] = j_coeff*(*Vx)[i][z_dof];
        }
      }
    }

    if (fp != Teuchos::null) {

      for (int i = 0; i < dirichlet_workset.num_cols_p; ++i) {
        for (int block = 0; block < nblock; ++block) {
          (*fp)[block][i][x_dof] =
          -x_val.dx(dirichlet_workset.param_offset+i).coeff(block);
          (*fp)[block][i][y_dof] =
          -y_val.dx(dirichlet_workset.param_offset+i).coeff(block);
          (*fp)[block][i][z_dof] =
          -z_val.dx(dirichlet_workset.param_offset+i).coeff(block);
        }
      }
    }

  }
}
#endif

}
  // namespace LCM
