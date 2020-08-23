#include <polyfem/NLProblem.hpp>

#include <polysolve/LinearSolver.hpp>
#include <polysolve/FEMSolver.hpp>

#include <polyfem/Types.hpp>

#include <ipc.hpp>

#include <unsupported/Eigen/SparseExtra>

static bool disable_collision = true;

namespace polyfem
{
	using namespace polysolve;

	NLProblem::NLProblem(State &state, const RhsAssembler &rhs_assembler, const double t)
		: state(state), assembler(AssemblerUtils::instance()), rhs_assembler(rhs_assembler),
		  full_size((assembler.is_mixed(state.formulation()) ? state.n_pressure_bases : 0) + state.n_bases * state.mesh->dimension()),
		  reduced_size(full_size - state.boundary_nodes.size()),
		  t(t), rhs_computed(false), is_time_dependent(state.problem->is_time_dependent())
	{
		assert(!assembler.is_mixed(state.formulation()));
	}

	void NLProblem::init_timestep(const TVector &x_prev, const TVector &v_prev, const double dt)
	{
		this->x_prev = x_prev;
		this->v_prev = v_prev;
		this->dt = dt;
	}

	void NLProblem::update_quantities(const double t, const TVector &x)
	{
		if (is_time_dependent){
			v_prev = (x - x_prev) / dt;
			x_prev = x;
			rhs_computed = false;
			this->t = t;

			// rhs_assembler.set_velocity_bc(local_boundary, boundary_nodes, args["n_boundary_samples"], local_neumann_boundary, velocity, t);
			// rhs_assembler.set_acceleration_bc(local_boundary, boundary_nodes, args["n_boundary_samples"], local_neumann_boundary, acceleration, t);
		}
	}

	const Eigen::MatrixXd &NLProblem::current_rhs()
	{
		if (!rhs_computed)
		{
			rhs_assembler.compute_energy_grad(state.local_boundary, state.boundary_nodes, state.args["n_boundary_samples"], state.local_neumann_boundary, state.rhs, t, _current_rhs);
			rhs_computed = true;

			if (assembler.is_mixed(state.formulation()))
			{
				const int prev_size = _current_rhs.size();
				if (prev_size < full_size)
				{
					_current_rhs.conservativeResize(prev_size + state.n_pressure_bases, _current_rhs.cols());
					_current_rhs.block(prev_size, 0, state.n_pressure_bases, _current_rhs.cols()).setZero();
				}
			}
			assert(_current_rhs.size() == full_size);

			if (is_time_dependent)
			{
				const TVector tmp = state.mass*(x_prev + dt * v_prev);

				_current_rhs *= dt * dt / 2;
				_current_rhs += tmp;
			}
			rhs_assembler.set_bc(state.local_boundary, state.boundary_nodes, state.args["n_boundary_samples"], state.local_neumann_boundary, _current_rhs, t);
		}

		return _current_rhs;
	}


	bool NLProblem::is_step_valid(const TVector &x0, const TVector &x1)
	{
		if (disable_collision)
			return true;
		if (!state.args["has_collision"])
				return true;

		Eigen::MatrixXd full0, full1;
		if (x0.size() == reduced_size)
			reduced_to_full(x0, full0);
		else
			full0 = x0;
		if (x1.size() == reduced_size)
			reduced_to_full(x1, full1);
		else
			full1 = x1;
		assert(full0.size() == full_size);
		assert(full1.size() == full_size);

		const int problem_dim = state.mesh->dimension();
		Eigen::MatrixXd reshaped0(full0.size() / problem_dim, problem_dim);
		Eigen::MatrixXd reshaped1(full1.size() / problem_dim, problem_dim);
		for (int i = 0; i < full0.size(); i += problem_dim)
		{
			for (int d = 0; d < problem_dim; ++d){
				reshaped0(i / problem_dim, d) = full0(i + d);
				reshaped1(i / problem_dim, d) = full1(i + d);
			}
		}
		assert((full0.size() / problem_dim) * problem_dim == full0.size());
		assert(reshaped0(0, 0) == full0(0));
		assert(reshaped1(0, 0) == full1(0));
		assert(reshaped0(0, 1) == full0(1));
		assert(reshaped1(0, 1) == full1(1));

		// {
		// 	std::ofstream out("test0.obj");
		// 	for (int i = 0; i < state.boundary_nodes_pos.rows(); ++i)
		// 		out << "v " << state.boundary_nodes_pos(i, 0) + reshaped0(i, 0) << " " << state.boundary_nodes_pos(i, 1) + reshaped0(i, 1) << " 0\n";

		// 	for (int i = 0; i < state.boundary_edges.rows(); ++i)
		// 		out << "l " << state.boundary_edges(i, 0) + 1 << " " << state.boundary_edges(i, 1) + 1 << "\n";
		// 	out.close();
		// }

		// {
		// 	std::ofstream out("test1.obj");
		// 	for (int i = 0; i < state.boundary_nodes_pos.rows(); ++i)
		// 		out << "v " << state.boundary_nodes_pos(i, 0) + reshaped1(i, 0) << " " << state.boundary_nodes_pos(i, 1) + reshaped1(i, 1) << " 0\n";

		// 	for (int i = 0; i < state.boundary_edges.rows(); ++i)
		// 		out << "l " << state.boundary_edges(i, 0) + 1 << " " << state.boundary_edges(i, 1) + 1 << "\n";
		// 	out.close();
		// }

		// std::cout<<"state.boundary_nodes_pos + reshaped\n"<<full<<std::endl;
		// std::cout<<"state.boundary_nodes_pos + reshaped\n"<<reshaped<<std::endl;

		return !ipc::is_step_collision_free(state.boundary_nodes_pos + reshaped0, state.boundary_nodes_pos + reshaped1, state.boundary_edges, state.boundary_triangles);
	}

	double NLProblem::value(const TVector &x)
	{
		Eigen::MatrixXd full;
		if (x.size() == reduced_size)
			reduced_to_full(x, full);
		else
			full = x;
		assert(full.size() == full_size);

		const auto &gbases = state.iso_parametric() ? state.bases : state.geom_bases;

		const double elastic_energy = assembler.assemble_energy(rhs_assembler.formulation(), state.mesh->is_volume(), state.bases, gbases, full);
		const double body_energy = rhs_assembler.compute_energy(full, state.local_neumann_boundary, state.args["n_boundary_samples"], t);

		double intertia_energy = 0;
		double collision_energy = 0;
		double scaling = 1;

		if(is_time_dependent)
		{
			scaling = dt * dt / 2.0;
			const TVector tmp = full - (x_prev + dt * v_prev);

			intertia_energy = 0.5 * tmp.transpose() * state.mass * tmp;
		}

		if (!disable_collision && state.args["has_collision"])
		{
			const int problem_dim = state.mesh->dimension();
			Eigen::MatrixXd reshaped(full.size() / problem_dim, problem_dim);
			for (int i = 0; i < full.size(); i += problem_dim)
			{
				for(int d = 0; d < problem_dim; ++d)
					reshaped(i / problem_dim, d) = full(i + d);
			}
			assert(reshaped.rows() * problem_dim == full.size());
			assert(reshaped(0, 0) == full(0));
			assert(reshaped(0, 1) == full(1));

			// std::ofstream out("test.obj");
			// for (int i = 0; i < state.boundary_nodes_pos.rows(); ++i)
			// 	out << "v " << state.boundary_nodes_pos(i, 0) + reshaped(i, 0) << " " << state.boundary_nodes_pos(i, 1) + reshaped(i, 1) << " 0\n";

			// for (int i = 0; i < state.boundary_edges.rows(); ++i)
			// 	out << "l " << state.boundary_edges(i, 0) + 1 << " " << state.boundary_edges(i, 1) + 1 << "\n";
			// out.close();

			// std::cout<<"state.boundary_nodes_pos + reshaped\n"<<full<<std::endl;
			// std::cout<<"state.boundary_nodes_pos + reshaped\n"<<reshaped<<std::endl;

			double dhat_squared = 1e-6;
			ccd::Candidates constraint_set;
			ipc::construct_constraint_set(state.boundary_nodes_pos + reshaped, state.boundary_edges, state.boundary_triangles, dhat_squared, constraint_set);
			collision_energy = ipc::compute_barrier_potential(state.boundary_nodes_pos, state.boundary_nodes_pos + reshaped, state.boundary_edges, state.boundary_triangles, constraint_set, dhat_squared);

			std::cout << "collision_energy " << collision_energy << std::endl;
		}

		return scaling * (elastic_energy + body_energy + 1e8 * collision_energy) + intertia_energy;
	}

	void NLProblem::compute_cached_stiffness()
	{
		if (cached_stiffness.size() == 0)
		{
			const auto &gbases = state.iso_parametric() ? state.bases : state.geom_bases;
			if (assembler.is_linear(state.formulation()))
			{
				assembler.assemble_problem(state.formulation(), state.mesh->is_volume(), state.n_bases, state.bases, gbases, cached_stiffness);
			}
		}
	}

	void NLProblem::gradient(const TVector &x, TVector &gradv)
	{
		Eigen::MatrixXd grad;
		gradient_no_rhs(x, grad);

		if(is_time_dependent)
		{
			Eigen::MatrixXd full;
			if (x.size() == reduced_size)
				reduced_to_full(x, full);
			else
				full = x;
			assert(full.size() == full_size);

			grad *= dt * dt / 2.0;
			grad += state.mass * full;
		}

		grad -= current_rhs();

		full_to_reduced(grad, gradv);

		// std::cout<<"gradv\n"<<gradv<<"\n--------------\n"<<std::endl;
	}

	void NLProblem::gradient_no_rhs(const TVector &x, Eigen::MatrixXd &grad)
	{
		Eigen::MatrixXd full;
		if (x.size() == reduced_size)
			reduced_to_full(x, full);
		else
			full = x;
		assert(full.size() == full_size);

		const auto &gbases = state.iso_parametric() ? state.bases : state.geom_bases;
		assembler.assemble_energy_gradient(rhs_assembler.formulation(), state.mesh->is_volume(), state.n_bases, state.bases, gbases, full, grad);

		if (!disable_collision && state.args["has_collision"])
		{
			const int problem_dim = state.mesh->dimension();
			Eigen::MatrixXd reshaped(full.size() / problem_dim, problem_dim);
			for (int i = 0; i < full.size(); i += problem_dim)
			{
				for (int d = 0; d < problem_dim; ++d)
					reshaped(i / problem_dim, d) = full(i + d);
			}
			assert((full.size() / problem_dim) * problem_dim == full.size());
			assert(reshaped(0, 0) == full(0));
			assert(reshaped(0, 1) == full(1));

			double dhat_squared = 1e-6;
			ccd::Candidates constraint_set;
			ipc::construct_constraint_set(state.boundary_nodes_pos + reshaped, state.boundary_edges, state.boundary_triangles, dhat_squared, constraint_set);
			grad += 1e8 * ipc::compute_barrier_potential_gradient(state.boundary_nodes_pos, state.boundary_nodes_pos + reshaped, state.boundary_edges, state.boundary_triangles, constraint_set, dhat_squared);
			// std::cout << "collision grad " << ipc::compute_barrier_potential_gradient(state.boundary_nodes_pos, state.boundary_nodes_pos + reshaped, state.boundary_edges, state.boundary_triangles, constraint_set, dhat_squared).norm() << std::endl;
		}

		assert(grad.size() == full_size);
	}

	void NLProblem::hessian(const TVector &x, THessian &hessian)
	{
		THessian tmp;
		hessian_full(x, tmp);

		std::vector<Eigen::Triplet<double>> entries;

		Eigen::VectorXi indices(full_size);

		int index = 0;
		size_t kk = 0;
		for (int i = 0; i < full_size; ++i)
		{
			if (kk < state.boundary_nodes.size() && state.boundary_nodes[kk] == i)
			{
				++kk;
				indices(i) = -1;
				continue;
			}

			indices(i) = index++;
		}
		assert(index == reduced_size);

		for (int k = 0; k < tmp.outerSize(); ++k)
		{
			if (indices(k) < 0)
			{
				continue;
			}

			for (THessian::InnerIterator it(tmp, k); it; ++it)
			{
				// std::cout<<it.row()<<" "<<it.col()<<" "<<k<<std::endl;
				assert(it.col() == k);
				if (indices(it.row()) < 0 || indices(it.col()) < 0)
				{
					continue;
				}

				assert(indices(it.row()) >= 0);
				assert(indices(it.col()) >= 0);

				entries.emplace_back(indices(it.row()), indices(it.col()), it.value());
			}
		}

		hessian.resize(reduced_size, reduced_size);
		hessian.setFromTriplets(entries.begin(), entries.end());
		hessian.makeCompressed();
	}

	void NLProblem::hessian_full(const TVector &x, THessian &hessian)
	{
		Eigen::MatrixXd full;
		if (x.size() == reduced_size)
			reduced_to_full(x, full);
		else
			full = x;

		assert(full.size() == full_size);

		const auto &gbases = state.iso_parametric() ? state.bases : state.geom_bases;
		if (assembler.is_linear(rhs_assembler.formulation())){
			compute_cached_stiffness();
			hessian = cached_stiffness;
		}
		else
			assembler.assemble_energy_hessian(rhs_assembler.formulation(), state.mesh->is_volume(), state.n_bases, state.bases, gbases, full, hessian);
		if (is_time_dependent)
		{
			hessian *= dt * dt / 2;
			hessian += state.mass;
		}

		if (!disable_collision && state.args["has_collision"])
		{
			const int problem_dim = state.mesh->dimension();
			Eigen::MatrixXd reshaped(full.size() / problem_dim, problem_dim);
			for (int i = 0; i < full.size(); i += problem_dim)
			{
				for (int d = 0; d < problem_dim; ++d)
					reshaped(i / problem_dim, d) = full(i + d);
			}
			assert((full.size() / problem_dim) * problem_dim == full.size());
			assert(reshaped(0, 0) == full(0));
			assert(reshaped(0, 1) == full(1));

			double dhat_squared = 1e-6;
			ccd::Candidates constraint_set;
			ipc::construct_constraint_set(state.boundary_nodes_pos + reshaped, state.boundary_edges, state.boundary_triangles, dhat_squared, constraint_set);
			hessian += 1e8 * ipc::compute_barrier_potential_hessian(state.boundary_nodes_pos, state.boundary_nodes_pos + reshaped, state.boundary_edges, state.boundary_triangles, constraint_set, dhat_squared);
		}

		assert(hessian.rows() == full_size);
		assert(hessian.cols() == full_size);
		// Eigen::saveMarket(tmp, "tmp.mat");
		// exit(0);
	}

	void NLProblem::full_to_reduced(const Eigen::MatrixXd &full, TVector &reduced) const
	{
		full_to_reduced_aux(state, full_size, reduced_size, full, reduced);
	}

	void NLProblem::reduced_to_full(const TVector &reduced, Eigen::MatrixXd &full)
	{
		reduced_to_full_aux(state, full_size, reduced_size, reduced, current_rhs(), full);
	}
} // namespace polyfem
