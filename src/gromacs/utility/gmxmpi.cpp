#include "gmxmpi.h"

int gmx_mpi_get_rank()
{
    int rank = -1;
#ifdef GMX_LIB_MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#elif defined(GMX_THREAD_MPI)
    tMPI_Comm_rank(TMPI_COMM_WORLD, &rank);
#else
    rank = 0;
#endif
    return rank;
}

#define PHI_CARDS_PER_NODE 4
int gmx_mpi_get_phi_card()
{
	return gmx_mpi_get_rank() % PHI_CARDS_PER_NODE;
}
