//                                MFEM Example 1
//
// Compile with: make ex1
//

#include "mfem.hpp"
#include "Schwarz.hpp"

#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;

// constructor
patch_nod_info::patch_nod_info(Mesh * mesh_, int ref_levels_,Array<int> ess_dof_list) 
               : mesh(mesh_), ref_levels(ref_levels_) 
{
/* The patches are defined by all the "active" vertices of the coarse mesh
   We define a low order H1 fespace and perform refinements so that we can get
   the H1 prolongation operator recursively. This way we can easily find  
   all the patches that the fine mesh vertices contribute to. After the vertices 
   are done the edges, faces and elements can be found easily because they
   contribute to the same patches as their vertices. */

   // Number of patches
   nrpatch = mesh->GetNV();
   int dim = mesh->Dimension();
   FiniteElementCollection *fec = new H1_FECollection(1, dim);
   FiniteElementSpace *fespace = new FiniteElementSpace(mesh, fec);

   // First we need to construct a list of non-essential coarse grid vertices 

   SparseMatrix * Pr = nullptr;
   // 4. Refine the mesh 
   for (int i = 0; i < ref_levels; i++)
   {
      const FiniteElementSpace cfespace(*fespace);
      mesh->UniformRefinement();
      // Update fespace
      fespace->Update();
      OperatorHandle Tr(Operator::MFEM_SPARSEMAT);
      fespace->GetTransferOperator(cfespace, Tr);
      Tr.SetOperatorOwner(false);
      SparseMatrix * P;
      Tr.Get(P);
      if (!Pr)
      {
         Pr = P;
      }
      else
      {
         Pr = Mult(*P,*Pr);
      }
   }
   Pr->Threshold(0.0);
   int nvert = mesh->GetNV();
   vertex_contr.resize(nvert);
   for (int iv = 0; iv< nvert; iv++)
   {
      int nz = Pr->RowSize(iv);
      vertex_contr[iv].SetSize(nz);
      int *col = Pr->GetRowColumns(iv);
      for (int i = 0; i<nz; i++)
      {
         vertex_contr[iv][i] = col[i];   
      }
   }

   Array<int> edge_vertices;
   int nedge = mesh->GetNEdges();
   edge_contr.resize(nedge);
   for (int ie=0; ie< nedge; ie++ )
   {
      mesh->GetEdgeVertices(ie,edge_vertices);
      int nv = edge_vertices.Size(); // always 2 but ok
      // The edge will contribute to the same patches as its vertices
      for(int iv=0; iv< nv; iv++)
      {
         int ivert = edge_vertices[iv];
         edge_contr[ie].Append(vertex_contr[ivert]);
      }
      edge_contr[ie].Sort(); edge_contr[ie].Unique();
   }

   Array<int> face_vertices;
   int nface = mesh->GetNFaces();
   face_contr.resize(nface);
   for (int ifc=0; ifc< nface; ifc++ )
   {
      mesh->GetFaceVertices(ifc,face_vertices);
      int nv = face_vertices.Size(); 
      // The face will contribute to the same patches as its vertices
      for(int iv=0; iv< nv; iv++)
      {
         int ivert = face_vertices[iv];
         face_contr[ifc].Append(vertex_contr[ivert]);
      }
      face_contr[ifc].Sort(); face_contr[ifc].Unique();
   }

   Array<int> elem_vertices;
   int nelem = mesh->GetNE();
   elem_contr.resize(nelem);
   for (int iel=0; iel< nelem; iel++ )
   {
      mesh->GetElementVertices(iel,elem_vertices);
      int nv = elem_vertices.Size(); 
      // The element will contribute to the same patches as its vertices
      for(int iv=0; iv< nv; iv++)
      {
         int ivert = elem_vertices[iv];
         elem_contr[iel].Append(vertex_contr[ivert]);
      }
      elem_contr[iel].Sort(); elem_contr[iel].Unique();
   }
   delete fespace;
   delete fec;
}

// Constructor of patch local problems
patch_assembly::patch_assembly(Mesh * cmesh_, int ref_levels_,FiniteElementSpace *fespace,Array<int> ess_dof_list) 
               : cmesh(*cmesh_), ref_levels(ref_levels_) 
{
   patch_nod_info *patches = new patch_nod_info(&cmesh,ref_levels,ess_dof_list);

   nrpatch = patches->nrpatch;
   Pid.SetSize(nrpatch);
   // Build a sparse matrix out of this map to extract the patch submatrix
   Array<int> dofoffset(nrpatch); dofoffset = 0;
   int height = fespace->GetVSize();
   // allocation of sparse matrices.
   for (int i=0; i<nrpatch; i++)
   {
      Pid[i] = new SparseMatrix(height);
   }
   // Now the filling of the matrices with vertex,edge,face,interior dofs
   Mesh * mesh = fespace->GetMesh();
   int nrvert = mesh->GetNV();
   int nredge = mesh->GetNEdges();
   int nrface = mesh->GetNFaces();
   int nrelem = mesh->GetNE();
   // First the vertices
   for (int i=0; i< nrvert; i++ )
   {
      int np = patches->vertex_contr[i].Size();
      Array<int> vertex_dofs;
      fespace->GetVertexDofs(i,vertex_dofs);
      int nv = vertex_dofs.Size();

      for (int j=0; j<np ; j++)
      {
         int k = patches->vertex_contr[i][j];
         for (int l=0; l < nv; l++)
         {
            int m = vertex_dofs[l];
            Pid[k]->Set(m,dofoffset[k],1.0);
            dofoffset[k]++;
         }
      }
   }
    // Edges
   for (int i=0; i< nredge; i++ )
   {
      int np = patches->edge_contr[i].Size();
      Array<int> edge_dofs;
      fespace->GetEdgeInteriorDofs(i,edge_dofs);
      int ne = edge_dofs.Size();
      for (int j=0; j<np ; j++)
      {
         int k = patches->edge_contr[i][j];
         for (int l=0; l < ne; l++)
         {
            int m = edge_dofs[l];
            Pid[k]->Set(m,dofoffset[k],1.0);
            dofoffset[k]++;
         }
      }
   }
   // Faces
   for (int i=0; i< nrface; i++ )
   {
      int np = patches->face_contr[i].Size();
      Array<int> face_dofs;
      fespace->GetFaceInteriorDofs(i,face_dofs);
      int nfc = face_dofs.Size();
      for (int j=0; j<np ; j++)
      {
         int k = patches->face_contr[i][j];
         for (int l=0; l < nfc; l++)
         {
            int m = face_dofs[l];
            Pid[k]->Set(m,dofoffset[k],1.0);
            dofoffset[k]++;
         }
      }
   }

   // The following can be skipped in case of static condensation
   // Elements
   for (int i=0; i< nrelem; i++ )
   {
      int np = patches->elem_contr[i].Size();
      Array<int> elem_dofs;
      fespace->GetElementInteriorDofs(i,elem_dofs);
      int nel = elem_dofs.Size();
      for (int j=0; j<np ; j++)
      {
         int k = patches->elem_contr[i][j];
         for (int l=0; l < nel; l++)
         {
            int m = elem_dofs[l];
            Pid[k]->Set(m,dofoffset[k],1.0);
            dofoffset[k]++;
         }
      }
   }

   for (int i=0; i< nrpatch; i++ )
   {
      Pid[i]->SetWidth(dofoffset[i]);
      Pid[i]->Finalize();
   }   
}

// constructor
SchwarzSmoother::SchwarzSmoother(Mesh * cmesh_, int ref_levels_, FiniteElementSpace *fespace_,SparseMatrix *A_,Array<int> ess_dof_list)
   : Solver(A_->Height(), A_->Width()), A(A_) 
{

   StopWatch chrono;
   chrono.Clear();
   chrono.Start();   
   P = new patch_assembly(cmesh_,ref_levels_, fespace_, ess_dof_list);
   chrono.Stop();
   cout << "Total patch dofs info time " << chrono.RealTime() << "s. \n";

   nrpatch = P->nrpatch;
   A_local.SetSize(nrpatch);
   invA_local.SetSize(nrpatch);


   chrono.Clear();
   chrono.Start();   
   for (int i=0; i< nrpatch; i++ )
   {
      SparseMatrix * Pr = P->Pid[i];
      // construct the local problems. Factor the patch matrices
      A_local[i] = RAP(*Pr,*A,*Pr);
      invA_local[i] = new UMFPackSolver;
      invA_local[i]->Control[UMFPACK_ORDERING] = UMFPACK_ORDERING_METIS;
      invA_local[i]->SetOperator(*A_local[i]);   
   }

   chrono.Stop();
   // cout << "Matrix extraction and setting up UMFPACK time " << chrono.RealTime() << "s. \n";
}

bool SchwarzSmoother::IsEssential(int idof, Array<int> ess_dof_list)
{
   int found = ess_dof_list.FindSorted(idof);
   return !(found == -1);
}

void SchwarzSmoother::Mult(const Vector &r, Vector &z) const
{
   // Apply the smoother patch on the restriction of the residual
   Array<Vector> res_local(nrpatch);
   Array<Vector> sol_local(nrpatch);
   Array<Vector> zaux(nrpatch);
   z = 0.0;

   switch(sType)
   {
      case Schwarz::SmootherType::ADDITIVE:
      {
         for (int i=0; i< nrpatch; i++)
         {
            SparseMatrix * Pr = P->Pid[i];
            res_local[i].SetSize(Pr->NumCols()); 
            sol_local[i].SetSize(Pr->NumCols()); 
            Pr->MultTranspose(r,res_local[i]);
            invA_local[i]->Mult(res_local[i],sol_local[i]);
            zaux[i].SetSize(r.Size()); zaux[i]=0.0; 
            Pr->Mult(sol_local[i],zaux[i]); 
            z += zaux[i];
         }
         // z *= 0.5; // Relaxation coefficient: possibly needed for contraction
      }
      break;
      case Schwarz::SmootherType::MULTIPLICATIVE:
      {
      //   TODO
      }
      break;
      case Schwarz::SmootherType::SYM_MULTIPLICATIVE:
      {
      //   TODO
      }
      break;
   }
   
}
