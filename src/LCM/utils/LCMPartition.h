//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

// Define only if Zoltan is enabled
#if defined (ALBANY_LCM) && defined(ALBANY_ZOLTAN)

#if !defined(LCM_Partition_h)
#define LCM_Partition_h

#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <vector>

#include <boost/shared_ptr.hpp>

#include <zoltan_cpp.h>

#include <Teuchos_RCP.hpp>
#include <Teuchos_ArrayRCP.hpp>
#include <Teuchos_ParameterList.hpp>

#include <Albany_AbstractDiscretization.hpp>
#include <Albany_DiscretizationFactory.hpp>
#include <Albany_STKDiscretization.hpp>
#include <Albany_SolverFactory.hpp>
#include <Albany_Utils.hpp>

#include <Intrepid2_MiniTensor_Geometry.h>

namespace LCM {

///
/// A list of IDs
///
typedef std::vector<int>
IDList;

///
/// Maps topological object by its ID to adjacent topological objects
/// by their IDs. Objects may and usually live in different spaces.
///
typedef std::map<int, IDList>
AdjacencyMap;

///
/// A scalar quantity associated with a topological object.
///
typedef std::map<int, double>
ScalarMap;

///
/// Map for topologcal objects for which it is possible to associate points.
///
typedef std::map<int, Intrepid2::Vector<double>>
PointMap;

///
/// Useful to distinguish among different partitioning schemes.
///
namespace PARTITION {

enum Scheme {
  UNKNOWN,
  RANDOM,
  GEOMETRIC,
  HYPERGRAPH,
  KMEANS,
  SEQUENTIAL,
  KDTREE
};

}

//
/// Forward declarations
//
class ConnectivityArray;
class DualGraph;
class ZoltanHyperGraph;
struct KDTreeNode;

///
/// Cluster center for K-means filtering algorithm. See
/// An Efficient K-means Clustering Algorithm: Analysis and Implementation
/// T. Kanungo et al.
/// IEEE Transactions on Pattern Analysis and Machine Intelligence
/// 24(7) July 2002
///
struct ClusterCenter {

  Intrepid2::Vector<double>
  position;

  Intrepid2::Vector<double>
  weighted_centroid;

  Intrepid2::Index
  count;

};

///
/// Binary tree node for K-means filtering algorithm. See
/// An Efficient K-means Clustering Algorithm: Analysis and Implementation
/// T. Kanungo et al.
/// IEEE Transactions on Pattern Analysis and Machine Intelligence
/// 24(7) July 2002
///
struct KDTreeNode {
  std::string
  name;

  boost::shared_ptr<KDTreeNode>
  parent;

  // Children
  boost::shared_ptr<KDTreeNode>
  left;

  boost::shared_ptr<KDTreeNode>
  right;

  // Bounding box of cell
  Intrepid2::Vector<double>
  lower_corner;

  Intrepid2::Vector<double>
  upper_corner;

  // Weighted centroid and count
  Intrepid2::Vector<double>
  weighted_centroid;

  Intrepid2::Index
  count;

  std::set<Intrepid2::Index>
  cell_points;

  std::set<Intrepid2::Index>
  candidate_centers;

  Intrepid2::Index
  closest_center_to_midcell;

};

///
/// Binary tree for K-means filtering algorithm. See
/// An Efficient K-means Clustering Algorithm: Analysis and Implementation
/// T. Kanungo et al.
/// IEEE Transactions on Pattern Analysis and Machine Intelligence
/// 24(7) July 2002
///
template<typename Node>
class KDTree {
public:

  KDTree(std::vector<Intrepid2::Vector<double>> const & points,
      Intrepid2::Index const number_centers);

  boost::shared_ptr<Node> &
  get_root()
  {
    return root_;
  }

private:

  boost::shared_ptr<Node>
  root_;

};

///
/// Build KD tree of list of points.
/// \param point list
/// \return Boost shared pointer to root node of tree.
///
template<typename Node>
boost::shared_ptr<Node>
BuildKDTree(std::vector<Intrepid2::Vector<double>> const & points);

///
/// Create KD tree node.
/// \param point list
/// \return Boost shared pointer to node of tree if created, 0 otherwise.
///
template<typename Node>
boost::shared_ptr<Node>
CreateKDTreeNode(
    std::string const & name,
    boost::shared_ptr<Node> parent,
    std::vector<Intrepid2::Vector<double>> const & points,
    std::set<Intrepid2::Index> const & points_indices);

///
/// Visit Tree nodes recursively and
/// perform the action defined by the Visitor object.
///
template<typename Node, typename Visitor>
void
VisitTreeNode(Node & node, Visitor const & visitor);

///
/// Traverse a Tree and perform the action defined by the Visitor object.
///
template<typename Tree, typename Visitor>
void
TraverseTree(Tree & tree, Visitor const & visitor);

///
/// Output visitor for KDTree node.
///
template<typename Node>
struct OutputVisitor {
  void
  operator()(Node const & node) const;

  bool
  pre_stop(Node const & node) const;

  bool
  post_stop(Node const & node) const;
};

///
/// Filtering visitor for K-means algorithm.
///
template<typename Node, typename Center>
struct FilterVisitor {

  std::vector<Intrepid2::Vector<double>> & points;

  std::vector<Center> & centers;

  FilterVisitor(
      std::vector<Intrepid2::Vector<double>> & p,
      std::vector<Center> & c);

  void
  operator()(Node const & node) const;

  bool
  pre_stop(Node const & node) const;

  bool
  post_stop(Node const & node) const;
};

///
/// Simple connectivity array.
/// Holds coordinate array as well.
///
class ConnectivityArray {
public:

  ///
  /// Default constructor for Connectivity Array
  ///
  ConnectivityArray();

  ///
  /// Build array specifying input and output
  /// \param input_file Exodus II input file name
  /// \param output_file Exodus II output file name
  ///
  ConnectivityArray(
      std::string const & input_file,
      std::string const & output_file);

  ///
  /// \return Number of nodes on the array
  ///
  Intrepid2::Index
  GetNumberNodes() const;

  ///
  /// \return Number of elements in the array
  ///
  Intrepid2::Index
  GetNumberElements() const;

  ///
  /// \return Space dimension
  ///
  Intrepid2::Index
  GetDimension() const;

  ///
  /// \return Type of finite element in the array
  /// (assume same type for all elements)
  ///
  Intrepid2::ELEMENT::Type
  GetType() const;

  ///
  /// \return Number of nodes that define element topology
  /// (assume same type for all elements)
  ///
  Intrepid2::Index
  GetNodesPerElement() const;

  ///
  /// \return Node ID and associated point in space
  ///
  PointMap
  GetNodeList() const;

  ///
  /// \return Element - nodes connectivity
  ///
  AdjacencyMap
  GetConnectivity() const;

  ///
  /// \return Volume for each element
  ///
  ScalarMap
  GetVolumes() const;

  ///
  /// \return Total volume of the array
  ///
  double
  GetVolume() const;

  ///
  /// \return Partitions when partitioned
  ///
  std::map<int, int>
  GetPartitions() const;

  ///
  /// \return Volume for each partition when partitioned
  ///
  ScalarMap
  GetPartitionVolumes() const;

  ///
  /// \return Partition centroids
  ///
  std::vector<Intrepid2::Vector<double>>
  GetPartitionCentroids() const;

  ///
  /// \return Centroids for each element
  ///
  PointMap
  GetCentroids() const;

  ///
  /// \return Bounding box for all nodes
  ///
  std::pair<Intrepid2::Vector<double>, Intrepid2::Vector<double>>
  BoundingBox() const;

  ///
  /// \param K-means tolerance
  ///
  void
  SetTolerance(double tolerance);

  ///
  /// \return K-means tolerance
  ///
  double
  GetTolerance() const;

  ///
  /// \param requested cell size for voxelization
  ///
  void
  SetCellSize(double requested_cell_size);

  ///
  /// \return requested cell size for voxelization
  ///
  double
  GetCellSize() const;

  ///
  /// \param maximum iterations for K-means
  ///
  void
  SetMaximumIterations(Intrepid2::Index maximum_iterations);

  ///
  /// \return maximum iterarions for K-means
  ///
  Intrepid2::Index
  GetMaximumIterations() const;

  ///
  /// \param Initializer scheme
  ///
  void
  SetInitializerScheme(PARTITION::Scheme initializer_scheme);

  ///
  /// \return Initializer scheme
  ///
  PARTITION::Scheme
  GetInitializerScheme() const;

  ///
  /// Validate for partitions with zero volume.
  ///
  void
  CheckNullVolume() const;

  ///
  /// Background grid of the domain for fast determination
  /// of points being inside or outside the domain.
  /// \return points inside the domain.
  ///
  std::vector<Intrepid2::Vector<double>>
  CreateGrid();

  ///
  /// Convert point to index into voxel array
  ///
  Intrepid2::Vector<int>
  PointToIndex(Intrepid2::Vector<double> const & point) const;

  ///
  /// Determine is a given point is inside the mesh.
  ///
  bool
  IsInsideMesh(Intrepid2::Vector<double> const & point) const;

  ///
  /// Determine is a given point is inside the mesh
  /// doing it element by element. Slow but useful
  /// to set up an initial data structure that will
  /// be used on a faster method.
  ///
  bool
  IsInsideMeshByElement(Intrepid2::Vector<double> const & point) const;

  ///
  /// \param length_scale Length scale for partitioning for
  /// variational non-local regularization
  /// \return Number of partitions defined as total volume
  /// of the array divided by the cube of the length scale
  ///
  Intrepid2::Index
  GetNumberPartitions(double const length_scale) const;

  ///
  /// \return Albany abstract discretization corresponding to array
  ///
  Albany::AbstractDiscretization &
  GetDiscretization();

  ///
  /// \param Collection of centers
  /// \return Partition map that assigns each element to the
  /// closest center to its centroid
  ///
  std::map<int, int>
  PartitionByCenters(std::vector<Intrepid2::Vector<double>> const & centers);

  ///
  /// Partition mesh with the specified algorithm and length scale
  /// \param partition_scheme The partition algorithm to use
  /// \param length_scale The length scale for variational nonlocal
  /// regularization
  /// \return Partition number for each element
  ///
  std::map<int, int>
  Partition(
      const PARTITION::Scheme partition_scheme,
      double const length_scale);

  ///
  /// Partition mesh with Zoltan Hypergraph algorithm
  /// \param length_scale The length scale for variational nonlocal
  /// regularization
  /// \return Partition number for each element
  ///
  std::map<int, int>
  PartitionHyperGraph(double const length_scale);

  ///
  /// Partition mesh with Zoltan Recursive Inertial Bisection algorithm
  /// \param length_scale The length scale for variational nonlocal
  /// regularization
  /// \return Partition number for each element
  ///
  std::map<int, int>
  PartitionGeometric(double const length_scale);

  ///
  /// Partition mesh with K-means algorithm
  /// \param length_scale The length scale for variational nonlocal
  /// regularization
  /// \return Partition number for each element
  ///
  std::map<int, int>
  PartitionKMeans(double const length_scale);

  ///
  /// Partition mesh with K-means algorithm and KD-tree
  /// \param length_scale The length scale for variational nonlocal
  /// regularization
  /// \return Partition number for each element
  ///
  std::map<int, int>
  PartitionKDTree(double const length_scale);

  ///
  /// Partition mesh with sequential K-means algorithm
  /// \param length_scale The length scale for variational nonlocal
  /// regularization
  /// \return Partition number for each element
  ///
  std::map<int, int>
  PartitionSequential(double const length_scale);

  ///
  /// Partition mesh with randomly generated centers.
  /// Mostly used to initialize other schemes.
  /// \param length_scale The length scale for variational nonlocal
  /// regularization
  /// \return Partition number for each element
  ///
  std::map<int, int>
  PartitionRandom(double const length_scale);

  ///
  /// Zoltan interface query function that returns the number of values
  /// needed to express the geometry of an object.
  /// For a three-dimensional object, the return value should be three.
  ///
  /// \param   data  Pointer to user-defined data.
  ///
  /// \param   ierr  Error code to be set by function.
  ///
  /// \return  The number of values needed to express the
  /// geometry of an object.
  ///
  static int
  GetNumberGeometry(
      void* data,
      int* ierr);

  ///
  /// Zoltan interface query function that returns the number of objects
  /// that are currently assigned to the processor.
  ///
  /// \param    data Pointer to user-defined data.
  ///
  /// \param    ierr Error code to be set by function.
  ///
  /// \return   int The number of objects that are assigned to the processor.
  ///
  static int
  GetNumberOfObjects(
      void* data,
      int* ierr);

  ///
  /// Zoltan interface query function that fills two
  /// (three if weights are used) arrays with information about
  /// the objects currently assigned to the processor.
  /// Both arrays are allocated (and subsequently freed) by Zoltan;
  /// their size is determined by a call to the
  /// ZoltanHyperGraph::GetNumberOfObjects query function
  /// to get the array size.
  ///
  /// \param data Pointer to user-defined data.
  ///
  /// \param sizeGID The number of array entries used to describe a
  /// single global ID.  This value is the maximum value over all processors
  /// of the parameter NUM_GID_ENTRIES.
  ///
  /// \param sizeLID The number of array entries used to describe a
  /// single local ID.  This value is the maximum value over all processors
  /// of the parameter NUM_LID_ENTRIES. (It should be zero if local ids
  /// are not used.)
  ///
  /// \param globalID  Upon return, an array of unique global IDs for
  /// all objects assigned to the processor.
  ///
  /// \param localID Upon return, an array of local IDs, the meaning
  /// of which can be determined by the application, for all objects
  /// assigned to the processor. (Optional.)
  ///
  /// \param wgt_dim The number of weights associated with an object
  /// (typically 1), or 0 if weights are not requested.
  /// This value is set through the parameter OBJ_WEIGHT_DIM.
  ///
  /// \param obj_wgts  Upon return, an array of object weights.
  /// Weights for object i are stored in obj_wgts[(i-1)*wgt_dim:i*wgt_dim-1].
  /// If wgt_dim=0, the return value of obj_wgts is undefined and may be NULL.
  ///
  /// \param ierr Error code to be set by function.
  ///
  static void
  GetObjectList(
      void* data,
      int sizeGID,
      int sizeLID,
      ZOLTAN_ID_PTR globalID,
      ZOLTAN_ID_PTR localID,
      int wgt_dim,
      float* obj_wgts,
      int* ierr);

  ///
  /// Zoltan interface query function that returns a vector of geometry
  /// values for a list of given objects. The geometry vector is allocated
  /// by Zoltan to be of size num_obj * num_dim;
  /// its format is described below.
  ///
  /// \param data Pointer to user-defined data.
  ///
  /// \param sizeGID The number of array entries used to describe a
  /// single global ID.  This value is the maximum value over all processors
  /// of the parameter NUM_GID_ENTRIES.
  ///
  /// \param sizeLID The number of array entries used to describe a
  /// single local ID.  This value is the maximum value over all processors
  /// of the parameter NUM_LID_ENTRIES. (It should be zero if local ids
  /// are not used.)
  ///
  /// \param num_obj The number of object IDs in arrays
  /// globalID and localID
  ///
  /// \param globalID  Upon return, an array of unique global IDs for
  /// all objects assigned to the processor.
  ///
  /// \param localID Upon return, an array of local IDs, the meaning
  /// of which can be determined by the application, for all objects
  /// assigned to the processor. (Optional.)
  ///
  /// \param num_dim Number of coordinate entries per object
  /// (typically 1, 2, or 3).
  ///
  /// \param geom_vec  Upon return, an array containing geometry values.
  /// For object i (specified by globalID[i*sizeGID] and
  /// localID[i*sizeLID], i=0,1,...,num_obj-1),
  /// coordinate values should be stored in
  /// geom_vec[i*num_dim:(i+1)*num_dim-1].
  ///
  /// \param ierr Error code to be set by function.
  ///
  static void
  GetGeometry(
      void* data,
      int sizeGID,
      int sizeLID,
      int num_obj,
      ZOLTAN_ID_PTR globalID,
      ZOLTAN_ID_PTR localID,
      int num_dim,
      double* geom_vec,
      int* ierr);

private:

  //
  // The type of elements in the mesh (assumed that all are of same type)
  //
  Intrepid2::ELEMENT::Type
  type_;

  //
  // Node list
  //
  PointMap
  nodes_;

  //
  // Element - nodes connectivity
  //
  AdjacencyMap
  connectivity_;

  //
  // Space dimension
  //
  Intrepid2::Index
  dimension_;

  //
  // Teuchos pointer to corresponding discretization
  //
  Teuchos::RCP<Albany::AbstractDiscretization>
  discretization_ptr_;

  //
  // Partitions if mesh is partitioned; otherwise empty
  //
  std::map<int, int>
  partitions_;

  //
  // Background grid of the domain for fast determination
  // of whether a point is inside the domain or not.
  //
  std::vector<std::vector<std::vector<bool>>>
  cells_;

  //
  // Size of background grid cell
  //
  Intrepid2::Vector<double>
  cell_size_;

  //
  // Parameters for kmeans partitioning
  //
  double
  tolerance_;

  double
  requested_cell_size_;

  Intrepid2::Index
  maximum_iterations_;

  //
  // Limits of the bounding box for coordinate array
  //
  Intrepid2::Vector<double>
  lower_corner_;

  Intrepid2::Vector<double>
  upper_corner_;

  //
  // Initializer scheme, if any.
  //
  PARTITION::Scheme
  initializer_scheme_;

};

///
/// Dual graph of a connectivity array
///
class DualGraph {

public:

  ///
  /// Default constructor
  ///
  DualGraph();

  ///
  /// Build dual graph from a connectivity array
  ///
  DualGraph(ConnectivityArray const & connectivity_array);

  ///
  /// \return Number of vertices in the dual graph
  ///
  int
  GetNumberVertices() const;

  ///
  /// \return Number of edges in the dual graph
  ///
  int
  GetNumberEdges() const;

  ///
  /// Set weights for dual graph vertices
  /// \param vw Map from vertex ID to weight
  ///
  void
  SetVertexWeights(ScalarMap & vertex_weights);

  ///
  /// \return Vertex weights of dual graph, if any
  ///
  ScalarMap
  GetVertexWeights() const;

  ///
  /// Replace current graph structure
  /// \param graph Graph structure that will replace the current one
  ///
  void
  SetGraph(AdjacencyMap & graph);

  ///
  /// \return Current graph structure
  ///
  AdjacencyMap
  GetGraph() const;

  ///
  /// \return Edge list to create boost graph
  ///
  AdjacencyMap
  GetEdgeList() const;

  ///
  /// \return Connected components in the dual graph
  ///
  int
  GetConnectedComponents(std::vector<int> & components) const;

  ///
  /// Print graph for debugging
  ///
  void
  Print() const;

private:

  //
  // Given a connectivity array type, return local numbering of
  // proper faces
  //
  std::vector<std::vector<int>>
  GetFaceConnectivity(Intrepid2::ELEMENT::Type const type) const;

private:

  //
  // Number of edges in dual graph
  //
  int
  number_edges_;

  //
  // Graph data structure
  //
  AdjacencyMap
  graph_;

  //
  // Vertex weights
  //
  ScalarMap
  vertex_weights_;

};

///
/// Class to interface with Zoltan HyperGraph
/// Hypergraph is represented in compressed vertex
/// storage format. See
/// http://www.cs.sandia.gov/Zoltan/ug_html/ug_query_lb.html#ZOLTAN_HG_CS_FN
///
/// The Zoltan interface functions must be static for linking purposes
/// (i.e. no hidden *this parameter)
/// See Zoltan documentation at
/// http://www.cs.sandia.gov/Zoltan/ug_html/ug.html
///
class ZoltanHyperGraph {

public:

  ///
  /// Default constructor
  ///
  ZoltanHyperGraph();

  ///
  /// Build Zoltan hypergraph from dual graph
  /// \param dual_graph Dual graph
  ///
  ZoltanHyperGraph(DualGraph const & dual_graph);

  ///
  /// \return Number of vertices in hypergraph
  ///
  int
  GetNumberVertices() const;

  ///
  /// Set number of hyperedges
  /// \param number_hyperedges Number of hyperedges
  ///
  void
  SetNumberHyperedges(int number_hyperedges);

  ///
  /// \return Number of hyperedges
  ///
  int
  GetNumberHyperedges() const;

  ///
  /// Replace current graph structure
  /// \param graph Graph structure that replaces current one
  ///
  void
  SetGraph(AdjacencyMap & graph);

  ///
  /// \return Current graph structure
  ///
  AdjacencyMap
  GetGraph() const;

  ///
  /// Set weights for hypergraph vertices
  /// \param vertex_weights Map from vertex ID to weight
  ///
  void
  SetVertexWeights(ScalarMap & vertex_weights);

  ///
  /// \return Vertex weights of hypergraph, if any.
  ///
  ScalarMap
  GetVertexWeights() const;

  ///
  /// \return Zoltan IDs for hyperedges.
  ///
  std::vector<ZOLTAN_ID_TYPE>
  GetEdgeIDs() const;

  ///
  /// \return Offsets into array of hyperedges that are attached
  /// to a vertex.
  ///
  std::vector<int>
  GetEdgePointers() const;

  ///
  /// \return Array with Zoltan vertex IDs
  ///
  std::vector<ZOLTAN_ID_TYPE>
  GetVertexIDs() const;

  ///
  /// Zoltan interface query function that returns the number of objects
  /// that are currently assigned to the processor.
  ///
  /// \param    data Pointer to user-defined data.
  ///
  /// \param    ierr Error code to be set by function.
  ///
  /// \return   int The number of objects that are assigned to the processor.
  ///
  static int
  GetNumberOfObjects(
      void* data,
      int* ierr);

  ///
  /// Zoltan interface query function that fills two
  /// (three if weights are used) arrays with information about
  /// the objects currently assigned to the processor.
  /// Both arrays are allocated (and subsequently freed) by Zoltan;
  /// their size is determined by a call to the
  /// ZoltanHyperGraph::GetNumberOfObjects query function
  /// to get the array size.
  ///
  /// \param data Pointer to user-defined data.
  ///
  /// \param sizeGID The number of array entries used to describe a
  /// single global ID.  This value is the maximum value over all processors
  /// of the parameter NUM_GID_ENTRIES.
  ///
  /// \param sizeLID The number of array entries used to describe a
  /// single local ID.  This value is the maximum value over all processors
  /// of the parameter NUM_LID_ENTRIES. (It should be zero if local ids
  /// are not used.)
  ///
  /// \param globalID  Upon return, an array of unique global IDs for
  /// all objects assigned to the processor.
  ///
  /// \param localID Upon return, an array of local IDs, the meaning
  /// of which can be determined by the application, for all objects
  /// assigned to the processor. (Optional.)
  ///
  /// \param wgt_dim The number of weights associated with an object
  /// (typically 1), or 0 if weights are not requested.
  /// This value is set through the parameter OBJ_WEIGHT_DIM.
  ///
  /// \param obj_wgts  Upon return, an array of object weights.
  /// Weights for object i are stored in obj_wgts[(i-1)*wgt_dim:i*wgt_dim-1].
  /// If wgt_dim=0, the return value of obj_wgts is undefined and may be NULL.
  ///
  /// \param ierr Error code to be set by function.
  ///
  static void
  GetObjectList(
      void* data,
      int sizeGID,
      int sizeLID,
      ZOLTAN_ID_PTR globalID,
      ZOLTAN_ID_PTR localID,
      int wgt_dim,
      float* obj_wgts,
      int* ierr);

  ///
  /// Zoltan interface query function to tell Zoltan in which format
  /// the application will supply the hypergraph, how many vertices and
  /// hyperedges there will be, and how many pins.
  /// The actual hypergraph is supplied with the query function
  /// ZoltanHyperGraph::GetHyperGraph
  ///
  /// \param data  Pointer to user-defined data.
  ///
  /// \param num_lists Upon return, the number of vertices
  /// (if using compressed vertex storage) or hyperedges
  /// (if using compressed hyperedge storage)
  /// that will be supplied to Zoltan by the application process.
  ///
  /// \param num_pins  Upon return, the number of pins
  /// (connections between vertices and hyperedges)
  /// that will be supplied to Zoltan by the application process.
  ///
  /// \param format  Upon return, the format in which
  /// the application process will provide the hypergraph to Zoltan.
  /// The options are ZOLTAN_COMPRESSED_EDGE and ZOLTAN_COMPRESSED_VERTEX.
  ///
  /// \param ierr  Error code to be set by function.
  ///
  static void
  GetHyperGraphSize(
      void* data,
      int* num_lists,
      int* num_pins,
      int* format,
      int* ierr);

  ///
  /// Zoltan interface function that returns the hypergraph in
  /// a compressed storage (CS) format.
  /// The size and format of the data to be returned must be supplied to
  /// Zoltan using the ZoltanHyperGraph::GetHyperGraphSize function.
  ///
  /// \param data Pointer to user-defined data.
  ///
  /// \param num_gid_entries The number of array entries used to
  /// describe a single global ID.
  /// This value is the maximum value over all processors of the
  /// parameter NUM_GID_ENTRIES.
  ///
  /// \param num_vtx_edge  The number of global IDs that is expected
  /// to appear on return in vtxedge_GID. This may correspond to either
  /// vertices or (hyper-)edges.
  ///
  /// \param num_pins  The number of pins that is expected to appear
  /// on return in pin_GID.
  ///
  /// \param format  If format is ZOLTAN_COMPRESSED_EDGE,
  /// Zoltan expects that hyperedge global IDs will be returned in
  /// vtxedge_GID, and that vertex global IDs will be returned in pin_GIDs.
  /// If it is ZOLTAN_COMPRESSED_VERTEX, then vertex global IDs are
  /// expected to be returned in vtxedge_GID and hyperedge global IDs are
  /// expected to be returned in pin_GIDs.
  ///
  /// \param vtxedge_GID Upon return, a list of num_vtx_edge global IDs.
  ///
  /// \param vtxedge_ptr Upon return, this array contains num_vtx_edge
  /// integers such that the number of pins specified for hyperedge j
  /// (if format is ZOLTAN_COMPRESSED_EDGE) or vertex j
  /// (if format is ZOLTAN_COMPRESSED_VERTEX) is
  /// vtxedge_ptr[j+1]-vtxedge_ptr[j]. If format is ZOLTAN_COMPRESSED_EDGE,
  /// vtxedge_ptr[j]*num_gid_entries is the index into the array pin_GID
  /// where edge j's pins (vertices belonging to edge j) begin;
  /// if format is ZOLTAN_COMPRESSED_VERTEX, vtxedge_ptr[j]*num_gid_entries
  /// is the index into the array pin_GID where vertex j's pins
  /// (edges to which vertex j belongs) begin. Array indices begin at zero.
  ///
  /// \param pin_GID Upon return, a list of num_pins global IDs.
  /// This is the list of the pins contained in the hyperedges or
  /// vertices listed in vtxedge_GID.
  ///
  /// \param ierr  Error code to be set by function.
  ///
  static void
  GetHyperGraph(
      void* data,
      int num_gid_entries,
      int num_vtx_edge,
      int num_pins,
      int format,
      ZOLTAN_ID_PTR
      vtxedge_GID,
      int* vtxedge_ptr,
      ZOLTAN_ID_PTR pin_GID,
      int* ierr);

private:

  //
  // Number of vertices
  //
  int
  number_vertices_;

  //
  // Number of hyperedges
  //
  int
  number_hyperedges_;

  //
  // Graph data structure
  //
  AdjacencyMap
  graph_;

  //
  // Vertex weights
  //
  ScalarMap
  vertex_weights_;

};

///
/// Read a Conectivity Array from an input stream
/// \param input_stream Input stream
/// \param connectivity_array Connectivity array
///
std::istream &
operator>>(
    std::istream & input_stream,
    ConnectivityArray & connectivity_array);

///
/// Write a Connectivity Array to an output stream
/// \param output_stream Output stream
/// \param connectivity_array Connectivity array
///
std::ostream &
operator<<(
    std::ostream & output_stream,
    ConnectivityArray const & connectivity_array);

///
/// Read a Zoltan Hyperedge Graph from an input stream
/// \param input_stream Input stream
/// \param zoltan_hypergraph Zoltan Hypergraph
///
std::istream &
operator>>(
    std::istream & input_stream,
    ZoltanHyperGraph & zoltan_hypergraph);

///
/// Write a Zoltan Hyperedge Graph to an output stream
/// \param output_stream Output stream
/// \param zoltan_hypergraph Zoltan Hypergraph
///
std::ostream &
operator<<(
    std::ostream & output_stream,
    ZoltanHyperGraph const & zoltan_hypergraph);

} // namespace LCM

#endif // #if !defined(LCM_Partition_h)

#endif // #if defined (ALBANY_LCM) && defined(ALBANY_ZOLTAN)
