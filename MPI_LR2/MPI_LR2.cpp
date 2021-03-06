// MPI_LR2.cpp: определяет точку входа для консольного приложения.
//

#include "stdafx.h"
#include "mpi.h"
#include <math.h>
#include <random>
#include <time.h>
#include <windows.h>


int N = 6000; //Размер матрицы
const int MaxRandomNumber = 10; //Максимальный диапазон чисел, которыми будет заполняться матрица
const int MinRandomNumber = 0;// Минимальный диапазон числе которыми будет заплняться матрица

int Rank;
int Size;
double* PartOfMatrix; //Часть сходной матрицы
double* B; //Часть вектора уравнений
int MatrixSize; //Количество уравнений на текущем ядре
int NodeWithMax; //Узел с текущим максимальным элементом
int Maxi = 0, Maxj = 0; //Индексы максимального элемента на текущем узле

/*------------------------------------------------------------------
Фукнция генерирующая часть матрицы только для оного конкретного узла.

На каждое ядро будет генерироваться 1/n часть строк матрицы
Если это конечно же возможно.
Если заданное количество строк матрицы не делится на число процессоров нацело
То первые k процессоров (где k - это остаток от деления количесства строк мартицы на число процессоров) будут генерировать на одну строку больше.
При этом строки распределяются по процессорам циклично (Тобишь (если 2 процессора) 1 строка на 1 процессоре, 2 на втором, 3 на первом 4 на втором и тд)
---------------------------------------------------------------------*/
void GeneratePartOfMatrix()
{
	//Инициализируем часть матрицы и часть вектора решений
	if (Rank < N%Size)
	{
		MatrixSize = N / Size + 1;
		PartOfMatrix = (double *)calloc(MatrixSize*N, sizeof(double));
		B = (double *)calloc(MatrixSize, sizeof(double));
	}
	else
	{
		MatrixSize = N / Size;
		PartOfMatrix = (double *)calloc(MatrixSize*N, sizeof(double));
		B = (double *)calloc(MatrixSize, sizeof(double));
	}
	
	//Наполняем часть марицы и вектора решений радномными числами
	for (int i = 0; i < MatrixSize; i++)
	{
		for (int j = 0; j < N; j++)
		{
			PartOfMatrix[i*N + j] = rand() % (MaxRandomNumber - MinRandomNumber) + MinRandomNumber;
		} 
		B[i] = rand() % (MaxRandomNumber - MinRandomNumber) + MinRandomNumber;
	}
}


/*---------------------------------------------------------------------
Функция поиска главного элемента в матрице

Сначала каждый узел найдет свой максимальный элемент.
Затем один узел будет искать максимальный элемент среди всех максимальных элементов узлов.
На вход принимает начальную строку в "глобальной матрице" с которой начинаем считать максимальный элемент
---------------------------------------------------------------------*/
void FindMaxOfMatrix(int StartNode, int StartRow, int iter)
{
	//Определяем с какой строки мы должны искать максимальный элемент в матрице
	int StartIndex = Rank >= StartNode ? StartRow: StartRow+1;
	//Для каждого узла находим максимальный элемент в его массиве
	double LocalMax = PartOfMatrix[StartIndex*N];
	int Maxi = iter;
	int Maxj = StartIndex;
	for (int i = iter; i < N; i++)
	{
		for (int j = StartIndex; j < MatrixSize; j++)
		{
			if (PartOfMatrix[j*N + i] > LocalMax)
			{
				LocalMax = PartOfMatrix[j*N + i];
				Maxi = i;
				Maxj = j;
			}
		}
	}

	StartIndex = NULL;

	struct { double val; int rank; } localmax;
	localmax.rank = Rank;
	localmax.val = LocalMax;
	struct { double val; int rank; } gmax;
	MPI_Allreduce(&localmax,&gmax,1,MPI_DOUBLE_INT,MPI_MAXLOC, MPI_COMM_WORLD);

	MPI_Bcast(&Maxj, 1, MPI_INT, gmax.rank, MPI_COMM_WORLD);
	MPI_Bcast(&Maxi, 1, MPI_INT, gmax.rank, MPI_COMM_WORLD);

}


/*---------------------------------------------------------------------------
Функция перемены строк
Меняет местами указанную строку в узле источнике и указанную  строку в узле назначения 
----------------------------------------------------------------------------*/
void SwapRows(int SourceRow, int SourceNode, int DestRow, int DestNode)
{	
	double* Buf;
	double* Recv;
	Buf = (double *)calloc(N, sizeof(double));
	Recv = (double *)calloc(N, sizeof(double));

	//Если нам нужно поменять строки из разных узлов
	if (SourceNode != DestNode)
	{
		if (Rank == SourceNode)
		{
			for (int i = 0; i < N; i++)
			{
				Buf[i] = PartOfMatrix[SourceRow*N + i];
			}

			MPI_Send(Buf, N, MPI_DOUBLE, DestNode, 1, MPI_COMM_WORLD);
			MPI_Recv(Recv, N, MPI_DOUBLE, DestNode, 1, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);

			for (int i = 0; i < N; i++)
			{
				PartOfMatrix[SourceRow*N + i] = Recv[i];
			}

		}
		if (Rank == DestNode)
		{
			for (int i = 0; i < N; i++)
			{
				Buf[i] = PartOfMatrix[DestRow*N + i];
			}

			MPI_Recv(Recv, N, MPI_DOUBLE, SourceNode, 1, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);
			MPI_Send(Buf, N, MPI_DOUBLE, SourceNode, 1, MPI_COMM_WORLD);

			for (int i = 0; i < N; i++)
			{
				PartOfMatrix[DestRow*N + i] = Recv[i];
			}
		}
		
	}
	//Если строки расположены на одном узле, то меняем их как в обычной однопоточной программе
	else
	{
		for (int i = 0; i < N; i++)
		{
			Buf[i] = PartOfMatrix[SourceRow*N + i];
			PartOfMatrix[SourceRow*N + i] = PartOfMatrix[DestRow*N + i];
			PartOfMatrix[DestRow*N + i] = Buf[i];
		}
	}
	free(Buf);
	free(Recv);
}


//Перестановка колонок в матрице
void SwapColumns(int SourceIndex, int DestIndex)
{
	for (int i = 0; i < MatrixSize; i++)
	{
		double Buf = PartOfMatrix[i*N + SourceIndex];
		PartOfMatrix[i*N + SourceIndex] = PartOfMatrix[i*N + DestIndex];
		PartOfMatrix[i*N + DestIndex] = Buf;
		Buf = NULL;
	}
}


//Прямой ход метода гаусса
void GaussForward()
{
	for (int i = 0; i < N-1; i++)
	{
		//Определяем узел на котором лежит данная строка
		int Node = i % Size;
		//Определяем строку в данном узле
		int Row = i / Size;
		//Находим максимальный элемент во всей матрице
		FindMaxOfMatrix(Node,Row,i);
		//Меняем местами колонки
		SwapColumns(i, Maxi);
		MPI_Barrier(MPI_COMM_WORLD);
		//Меняем местами строки
		if (Rank == NodeWithMax || Rank == Node)
		{
			SwapRows(Maxj, NodeWithMax, Row, Node);
		}
		MPI_Barrier(MPI_COMM_WORLD);
		//Организуем прямой проход по матрице
		double* RecvMass;
		RecvMass = (double *)calloc(N-Row+1, sizeof(double));
		if (Rank == Node)
		{
			for (int j = i; j < N; j++) {
				RecvMass[j - Row] = PartOfMatrix[Row*N + j];
			}
			RecvMass[N - Row] = B[Row];
		}

		//Отправляем строку с главвным элементом всем массивам
		//Последним элементов отправляем элемент с вектора ответов
		MPI_Bcast(RecvMass, N - Row +1, MPI_DOUBLE, Node, MPI_COMM_WORLD);

		//Если текущий узел меньше узла для в котором находится строка в главным элементом
		if (Rank <= Node)
		{
			for (int j = Row+1; j < MatrixSize; j++)
			{
				double Kef = (double)PartOfMatrix[j*N + i] / RecvMass[i-Row];
				for (int k = i; k < N; k++)
				{
					PartOfMatrix[j*N + k] -= (Kef)*RecvMass[k-Row];
				}
				B[j] -= Kef*RecvMass[N-Row];
			}
		}
		else
		{
			for (int j = Row; j < MatrixSize; j++)
			{
				double Kef = (double)PartOfMatrix[j*N + i] / RecvMass[i-Row];
				for (int k = i; k < N; k++)
				{
					PartOfMatrix[j*N + k] -= (Kef)*RecvMass[k-Row];
				}
				B[j] -= Kef*RecvMass[N-Row];
			}
		}		
	//	free(RecvMass);
	}
}

//Обратный проход в методе гаусса
void GaussBackward()
{
	for (int i = N - 1; i >= 0; i--)
	{
		//Определяем узел на котором лежит данная строка
		int Node = i % Size;
		//Определяем строку в данном узле
		int Row = i / Size;

		double *Elememts = (double *)calloc(2, sizeof(double));
		if (Rank == Node)
		{
			Elememts[0] = PartOfMatrix[Row*N + Row];
			Elememts[1] = B[Row];
		}

		MPI_Bcast(Elememts, 2, MPI_DOUBLE, Node, MPI_COMM_WORLD);

		if (Rank >= Node)
		{
			for (int j = Row-1; j >= 0; j--)
			{
				PartOfMatrix[j*N + i] = 0;
				B[j] -= (PartOfMatrix[j*N + Row] / Elememts[0])*Elememts[1];
			}
		}
		else
		{
			for (int j = Row; j >= 0; j--)
			{
				PartOfMatrix[j*N + i] = 0;
				B[j] -= (PartOfMatrix[j*N + Row] / Elememts[0])*Elememts[1];
			}
		}

		free(Elememts);
	}
}

int main(int argc, char *argv[])
{
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &Rank);
	MPI_Comm_size(MPI_COMM_WORLD, &Size);
	srand(time(NULL)+ Rank);
	GeneratePartOfMatrix();
	double StartTime = MPI_Wtime();

	MPI_Barrier(MPI_COMM_WORLD);
	GaussForward();
	MPI_Barrier(MPI_COMM_WORLD);
    GaussBackward();

	if (Rank ==0)
		printf("%f", MPI_Wtime() - StartTime);

	MPI_Finalize();
    return 0;
}

