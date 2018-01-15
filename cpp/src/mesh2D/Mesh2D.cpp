#include "Mesh2D.hpp"
#include "Navigation.hpp"

#include "MeshUtils.hpp"
#include "Refinement.hpp"

#include <igl/triangle/triangulate.h>
#include <igl/copyleft/tetgen/tetrahedralize.h>

#include <geogram/basic/file_system.h>
#include <geogram/mesh/mesh_io.h>
#include <geogram/mesh/mesh_geometry.h>
#include <geogram/mesh/mesh_repair.h>

#include <cassert>
#include <array>

namespace poly_fem
{
	void Mesh2D::refine(const int n_refiniment, const double t, std::vector<int> &parent_nodes)
	{
		// return;
		if(n_refiniment <= 0) return;

		for(int i = 0; i < n_refiniment; ++i)
		{
			GEO::Mesh mesh;
			mesh.copy(mesh_);
			mesh_.clear(false,false);

			//TODO add tags to the refiniment
			if(t<=0)
				refine_polygonal_mesh(mesh, mesh_, Polygons::catmul_clark_split_func());
			else
				refine_polygonal_mesh(mesh, mesh_, Polygons::polar_split_func(t));

			Navigation::prepare_mesh(mesh_);
		}

		compute_elements_tag();

		// save("test.obj");
	}

	bool Mesh2D::load(const std::string &path)
	{
		mesh_.clear(false,false);

		if(!mesh_load(path, mesh_))
			return false;


		GEO::vec3 min_corner, max_corner;
		GEO::get_bbox(mesh_, &min_corner[0], &max_corner[0]);
		GEO::vec3 extent = max_corner - min_corner;
		double scaling = std::max(extent[0], std::max(extent[1], extent[2]));
		const GEO::vec3 origin = min_corner;
		for (GEO::index_t v = 0; v < mesh_.vertices.nb(); ++v) {
			mesh_.vertices.point(v) = (mesh_.vertices.point(v) - origin) / scaling;
		}

		orient_normals_2d(mesh_);
		compute_elements_tag();
		Navigation::prepare_mesh(mesh_);
		return true;
	}

	bool Mesh2D::save(const std::string &path) const
	{
		if(!mesh_save(mesh_, path))
			return false;

		return true;
	}

	void Mesh2D::get_edges(Eigen::MatrixXd &p0, Eigen::MatrixXd &p1) const
	{
		p0.resize(mesh_.edges.nb(), 2);
		p1.resize(p0.rows(), p0.cols());

		for(GEO::index_t e = 0; e < mesh_.edges.nb(); ++e)
		{
			const int v0 = mesh_.edges.vertex(e, 0);
			const int v1 = mesh_.edges.vertex(e, 1);

			p0.row(e) = point(v0);
			p1.row(e) = point(v1);
		}
	}

	RowVectorNd Mesh2D::point(const int global_index) const {
		const double *ptr = mesh_.vertices.point_ptr(global_index);
		RowVectorNd p(2);
		p(0) = ptr[0];
		p(1) = ptr[1];
		return p;
	}

	void Mesh2D::fill_boundary_tags(std::vector<int> &tags) const
	{
		//TODO implement me correctly
		// tags.resize(mesh_.edges.nb());
		// std::fill(tags.begin(), tags.end(), -1);

		// Eigen::MatrixXd p0, p1, p;

		// for(GEO::index_t e = 0; e < mesh_.edges.nb(); ++e)
		// {
		// 	if(!is_boundary_edge((int) e)) {
		// 		continue;
		// 	}

		// 	const int v0 = mesh_.edges.vertex(e, 0);
		// 	const int v1 = mesh_.edges.vertex(e, 1);
		// 	point(v0, p0); point(v1, p1);

		// 	p = (p0 + p1)/2;

		// 	if(fabs(p(0))<1e-8)
		// 		tags[e]=1;
		// 	if(fabs(p(1))<1e-8)
		// 		tags[e]=2;
		// 	if(fabs(p(0)-1)<1e-8)
		// 		tags[e]=3;
		// 	if(fabs(p(1)-1)<1e-8)
		// 		tags[e]=4;
		// }
	}



	// Eigen::MatrixXd Mesh2D::node_from_face(const int face_id) const
	// {
	// 	Eigen::MatrixXd res=Eigen::MatrixXd::Zero(1, 2);
	// 	Eigen::MatrixXd pt;

	// 	for(int i = 0; i < n_element_vertices(face_id); ++i)
	// 	{
	// 		point(vertex_global_index(face_id, i), pt);
	// 		res += pt;
	// 	}

	// 	return res / n_element_vertices(face_id);
	// }

	// Eigen::MatrixXd Mesh2D::node_from_edge_index(const Navigation::Index &index) const
	// {
	// 	int id = switch_face(index).face;
	// 	if(id >= 0)
	// 	{
	// 		if(mesh_.facets.nb_vertices(id) == 4)
	// 			return node_from_face(id);
	// 	}

	// 	id = edge_node_id(index.edge);
	// 	assert(id >= 0);

	// 	GEO::Attribute<std::array<double, 3> > edges_node(mesh_.edges.attributes(), "edges_node");
	// 	Eigen::MatrixXd res(1, 2);

	// 	for(long i = 0; i < res.size(); ++i)
	// 		res(i) = edges_node[index.edge][i];

	// 	return res;
	// }

	// Eigen::MatrixXd Mesh2D::node_from_vertex(const int vertex_id) const
	// {
	// 	GEO::Attribute<int> vertices_node_id(mesh_.vertices.attributes(), "vertices_node_id");
	// 	assert(vertices_node_id[vertex_id] >= 0);

	// 	GEO::Attribute<std::array<double, 3>> vertices_node(mesh_.vertices.attributes(), "vertices_node");
	// 	Eigen::MatrixXd res(1, 2);
	// 	for(long i = 0; i < res.size(); ++i)
	// 		res(i) = vertices_node[vertex_id][i];

	// 	return res;
	// }

	// int Mesh2D::edge_node_id(const int edge_id) const
	// {
	// 	GEO::Attribute<int> edges_node_id(mesh_.edges.attributes(), "edges_node_id");
	// 	return edges_node_id[edge_id];
	// }

	// int Mesh2D::vertex_node_id(const int vertex_id) const
	// {
	// 	GEO::Attribute<int> vertices_node_id(mesh_.vertices.attributes(), "vertices_node_id");
	// 	return vertices_node_id[vertex_id];
	// }

	// bool Mesh2D::node_id_from_edge_index(const Navigation::Index &index, int &id) const
	// {
	// 	id = switch_face(index).face;
	// 	bool is_real_boundary = true;
	// 	if(id >= 0)
	// 	{
	// 		is_real_boundary = false;
	// 		if(mesh_.facets.nb_vertices(id) == 4)
	// 			return is_real_boundary;

	// 	}

	// 	id = edge_node_id(index.edge);
	// 	assert(id >= 0);

	// 	return is_real_boundary;
	// }

	// Eigen::MatrixXd Mesh2D::edge_mid_point(const int edge_id) const
	// {
	// 	Eigen::MatrixXd p0, p1;
	// 	const int v0 = mesh_.edges.vertex(edge_id, 0);
	// 	const int v1 = mesh_.edges.vertex(edge_id, 1);
	// 	point(v0, p0); point(v1, p1);
	// 	return (p0 + p1)/2;
	// }

	// void Mesh2D::create_boundary_nodes()
	// {
	// 	// Edge is on the mesh boundary, or at the interface with a polygon
	// 	std::vector<bool> boundary_or_interface(mesh_.edges.nb(), false);
	// 	for(GEO::index_t e = 0; e < mesh_.edges.nb(); ++e) {
	// 		if (is_boundary_edge((int) e)) { boundary_or_interface[e] = true; }
	// 	}
	// 	for(GEO::index_t f = 0; f < mesh_.facets.nb(); ++f) {
	// 		const int n_vertices = mesh_.facets.nb_vertices(f);
	// 		if(n_vertices > 4) {
	// 			Navigation::Index index = get_index_from_face(f);
	// 			for(int j = 0; j < n_vertices; ++j) {
	// 				boundary_or_interface[index.edge] = true;
	// 				index = next_around_face(index);
	// 			}
	// 		}
	// 	}

	// 	GEO::Attribute<int> edges_node_id(mesh_.edges.attributes(), "edges_node_id");
	// 	GEO::Attribute<std::array<double, 3> > edges_node(mesh_.edges.attributes(), "edges_node");

	// 	std::vector<int> vertex_counter(n_pts(), 0);

	// 	int counter = n_elements();

	// 	Eigen::MatrixXd p0, p1;

	// 	for (int e = 0; e < (int) mesh_.edges.nb(); ++e) {
	// 		if(!boundary_or_interface[e]) {
	// 			edges_node_id[e] = -1;
	// 			continue;
	// 		}

	// 		edges_node_id[e] = counter++;

	// 		const int v0 = mesh_.edges.vertex(e, 0);
	// 		const int v1 = mesh_.edges.vertex(e, 1);
	// 		point(v0, p0); point(v1, p1);
	// 		auto &val = (p0 + p1)/2;
	// 		for(long d = 0; d < val.size(); ++d) {
	// 			edges_node[e][d] = val(d);
	// 		}
	// 	}

	// 	GEO::Attribute<int> vertices_node_id(mesh_.vertices.attributes(), "vertices_node_id");
	// 	vertices_node_id.fill(-1);

	// 	GEO::Attribute<std::array<double, 3>> vertices_node(mesh_.vertices.attributes(), "vertices_node");

	// 	for (int e = 0; e < n_elements(); ++e) {
	// 		Navigation::Index index = get_index_from_face(e);

	// 		for(int i = 0; i < n_element_vertices(e); ++i) {
	// 			int prev = switch_edge(index).edge;
	// 			if (boundary_or_interface[prev] && boundary_or_interface[index.edge]) {
	// 				const int v_id = index.vertex;
	// 				vertices_node_id[v_id] = counter++;
	// 				point(v_id, p0);

	// 				for(long d = 0; d < p0.size(); ++d) {
	// 					vertices_node[v_id][d] = p0(d);
	// 				}
	// 			}

	// 			index = next_around_face(index);
	// 		}
	// 	}
	// }

	void Mesh2D::triangulate_faces(Eigen::MatrixXi &tris, Eigen::MatrixXd &pts, std::vector<int> &ranges) const
	{
		ranges.clear();

		std::vector<Eigen::MatrixXi> local_tris(mesh_.facets.nb());
		std::vector<Eigen::MatrixXd> local_pts(mesh_.facets.nb());

		int total_tris = 0;
		int total_pts  = 0;


		ranges.push_back(0);

		for(GEO::index_t f = 0; f < mesh_.facets.nb(); ++f)
		{
			const int n_vertices = mesh_.facets.nb_vertices(f);

			Eigen::MatrixXd face_pts(n_vertices, 2);
			Eigen::MatrixXi edges(n_vertices,2);

			for(int i = 0; i < n_vertices; ++i)
			{
				const int vertex = mesh_.facets.vertex(f,i);
				const double *pt = mesh_.vertices.point_ptr(vertex);
				face_pts(i, 0) = pt[0];
				face_pts(i, 1) = pt[1];

				edges(i, 0) = i;
				edges(i, 1) = (i+1) % n_vertices;
			}

			igl::triangle::triangulate(face_pts, edges, Eigen::MatrixXd(0,2), "QqYS0", local_pts[f], local_tris[f]);

			total_tris += local_tris[f].rows();
			total_pts  += local_pts[f].rows();

			ranges.push_back(total_tris);

			assert(local_pts[f].rows() == face_pts.rows());
		}


		tris.resize(total_tris, 3);
		pts.resize(total_pts, 2);

		int tri_index = 0;
		int pts_index = 0;
		for(std::size_t i = 0; i < local_tris.size(); ++i){
			tris.block(tri_index, 0, local_tris[i].rows(), local_tris[i].cols()) = local_tris[i].array() + pts_index;
			tri_index += local_tris[i].rows();

			pts.block(pts_index, 0, local_pts[i].rows(), local_pts[i].cols()) = local_pts[i];
			pts_index += local_pts[i].rows();
		}
	}

	void Mesh2D::compute_elements_tag()
	{
		elements_tag_.clear();
		poly_fem::compute_element_tags(mesh_, elements_tag_);
	}

	RowVectorNd Mesh2D::edge_barycenter(const int index) const
	{
		const int v0 = mesh_.edges.vertex(index, 0);
		const int v1 = mesh_.edges.vertex(index, 1);

		return 0.5*(point(v0) + point(v1));
	}

	RowVectorNd Mesh2D::face_barycenter(const int face_index) const
	{
		RowVectorNd bary(2); bary.setZero();

		const int n_vertices = n_face_vertices(face_index);
		Navigation::Index index = get_index_from_face(face_index);

		for(int lv = 0; lv < n_vertices; ++lv)
		{
			bary += point(index.vertex);
			index = next_around_face(index);
		}
		return bary / n_vertices;
	}

}
