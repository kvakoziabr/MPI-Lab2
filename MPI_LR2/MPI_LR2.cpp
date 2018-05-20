// MPI_LR2.cpp: определяет точку входа для консольного приложения.
//

#include "stdafx.h"
#include "mpi.h"
#include <math.h>
#include <random>
#include <time.h>
#include <windows.h>


const int N = 4; //Размер матрицы
const int MaxRandomNumber = 1000; //Максимальный диапазон чисел, которыми будет заполняться матрица
const int MinRandomNumber = 0;// Минимальный диапазон числе которыми будет заплняться матрица

int Rank;
int Size;
int* PartOfMatrix; //Часть сходной матрицы
int* B; //Часть вектора уравнений
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
		PartOfMatrix = (int *)calloc(MatrixSize*N, sizeof(int));
		B = (int *)calloc(MatrixSize, sizeof(int));
	}
	else
	{
		MatrixSize = N / Size;
		PartOfMatrix = (int *)calloc(MatrixSize*N, sizeof(int));
		B = (int *)calloc(MatrixSize, sizeof(int));
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
void FindMaxOfMatrix(int StartNode, int StartRow)
{
	//Для каждого узла находим максимальный элемент в его массиве
	int Max = PartOfMatrix[0];
	//Определяем с какой строки мы должны искать максимальный элемент в матрице
	int StartIndex = Rank > StartNode ? StartRow: StartRow+1;
	for (int i = 0; i < N; i++)
	{
		for (int j = StartIndex; j < MatrixSize; j++)
		{
			if (PartOfMatrix[j*N + i] > Max)
			{
				Max = PartOfMatrix[j*N + i];
				Maxi = i;
				Maxj = j;
			}
		}
	}
	StartIndex = NULL;

	//Синхронизируем все узлы
	MPI_Barrier(MPI_COMM_WORLD);

	if (Rank == StartNode)
	{
		//Устанавливаем нулевой узел, как текущий узел с максимальным элементом
		NodeWithMax = StartNode;
		//Получаем со всех узлов их максимальные элементы и находим максимальный элемент во всей матрице
		for (int i = 0; i < Size; i++)
		{
			//Принимаем и обрабатываем сообщения ото всех узлов, кроме конечно же текущего
			if (i != StartNode)
			{
				//Получаем максимальный элемент узла
				int MaxOfNode;
				MPI_Recv(&MaxOfNode, 1, MPI_INT, i, 1, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);
				//Сравниваем с текущим максимальным элементом
				if (MaxOfNode > Max)
				{
					//Если пришедший элемент больше максимального, то запоминаем максимальный элемент
					//И запоминаем узел с максимальный элементом
					Max = MaxOfNode;
					NodeWithMax = i;
				}
				MaxOfNode = NULL;
			}
		}
	}
	else
	{
		//Отправляем на нулеой узел максимальный элемент текущего узла
		MPI_Send(&Max, 1, MPI_INT, StartNode, 1, MPI_COMM_WORLD);
	}
	Max = NULL;
	//Отправляем все узлам данные о расположении максимального элемента
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Bcast(&NodeWithMax, 1, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(&Maxj, 1, MPI_INT, NodeWithMax, MPI_COMM_WORLD);
	MPI_Bcast(&Maxi, 1, MPI_INT, NodeWithMax, MPI_COMM_WORLD);
}


/*---------------------------------------------------------------------------
Функция перемены строк
Меняет местами указанную строку в узле источнике и указанную  строку в узле назначения 
----------------------------------------------------------------------------*/
void SwapRows(int SourceRow, int SourceNode, int DestRow, int DestNode)
{	
	int* Buf;
	int* Recv;
	Buf = (int *)calloc(N, sizeof(int));
	Recv = (int *)calloc(N, sizeof(int));

	//Если нам нужно поменять строки из разных узлов
	if (SourceNode != DestNode)
	{
		if (Rank == SourceNode)
		{
			for (int i = 0; i < N; i++)
			{
				Buf[i] = PartOfMatrix[SourceRow*N + i];
			}

			MPI_Send(Buf, N, MPI_INT, DestNode, 1, MPI_COMM_WORLD);
			MPI_Recv(Recv, N, MPI_INT, DestNode, 1, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);

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

			MPI_Recv(Recv, N, MPI_INT, SourceNode, 1, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);
			MPI_Send(Buf, N, MPI_INT, SourceNode, 1, MPI_COMM_WORLD);

			for (int i = 0; i < N; i++)
			{
				PartOfMatrix[DestRow*N + i] = Recv[i];
			}
		}
		
	}
	//Если строки расположены на одном узле, то меняем их как в обычной однопоточной программе
	else
	{
		if (Rank == SourceNode)
		{
			for (int i = 0; i < N; i++)
			{
				Buf[i] = PartOfMatrix[SourceRow*N + i];
			}

			for (int i = 0; i < N; i++)
			{
				PartOfMatrix[SourceRow*N + i] = PartOfMatrix[DestRow*N + i];
				PartOfMatrix[DestRow*N + i] = Buf[i];
			}
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
		int Buf = PartOfMatrix[i*N + SourceIndex];
		PartOfMatrix[i*N + SourceIndex] = PartOfMatrix[i*N + DestIndex];
		PartOfMatrix[i*N + DestIndex] = Buf;
		Buf = NULL;
	}
}


//Прямой ход метода гаусса
void GaussForward()
{
	for (int i = 0; i < 4; i++)
	{
		//Определяем узел на котором лежит данная строка
		int Node = i % Size;
		//Определяем строку в данном узле
		int Row = i / Size;
		//Находим максимальный элемент во всей матрице
		FindMaxOfMatrix(Node,Row);
		//Меняем местами колонки
		SwapColumns(i, Maxi);

		//Меняем местами строки
		MPI_Barrier(MPI_COMM_WORLD);
		if (Rank == NodeWithMax || Rank == Node)
			SwapRows(Maxj, NodeWithMax, Row, Node);

		//Организуем прямой проход по матрице
		int* RecvMass;
		RecvMass = (int *)calloc(N-Row+1, sizeof(int));
		if (Rank == Node)
		{
			for (int j = Row; j < N; j++) {
				RecvMass[j] = PartOfMatrix[Row*N + j];
			}
			RecvMass[N - Row] = B[Row];
		}

		//Отправляем строку с главвным элементом всем массивам
		//Последним элементов отправляем элемент с вектора ответов
		MPI_Bcast(RecvMass, N - Row +1, MPI_INT, Node, MPI_COMM_WORLD);

		//Если текущий узел меньше узла для в котором находится строка в главным элементом
		if (Rank <= Node)
		{
			for (int j = Row+1; j < MatrixSize; j++)
			{
				for (int k = i; k < N; k++)
				{
					PartOfMatrix[j*N + k] -= ((double)PartOfMatrix[j*N + i] / RecvMass[i])*RecvMass[k];

				}
			//	B[i] -= (PartOfMatrix[j*N + Row] / RecvMass[Row])*RecvMass[N];
			}
		}
		else
		{
			for (int j = Row; j < MatrixSize; j++)
			{
				for (int k = i; k < N; k++)
				{
					PartOfMatrix[j*N + k] -= ((double)PartOfMatrix[j*N + i] / RecvMass[i])*RecvMass[k];
				}

				//B[i] -= (PartOfMatrix[j*N + Row] / RecvMass[Row])*RecvMass[N];
			}
		}
		
		free(RecvMass);
	}
}

//Обратный проход в методе гаусса
void GaussBackward()
{
	for (int i = N - 1; i = 0; i--)
	{
		//Определяем узел на котором лежит данная строка
		int Node = (i + 1) % Size - 1;
		//Определяем строку в данном узле
		int Row = (i + 1) / Size;

		int *Elememts = (int *)calloc(2, sizeof(int));
		if (Rank == Node)
		{
			Elememts[0] = PartOfMatrix[Row*N + Row];
			Elememts[1] = B[Row];
		}

		MPI_Bcast(Elememts, 2, MPI_INT, Node, MPI_COMM_WORLD);

		if (Rank > Node)
		{
			for (int j = Rank-1; j = 0; j--)
			{
				PartOfMatrix[j*N + i] -= 0;
				//B[i] -= (PartOfMatrix[i*N + Row] / Elememts[0])*Elememts[1];
			}
		}
		else
		{
			for (int j = Rank; j = 0; j--)
			{
				PartOfMatrix[j*N + i] -= 0;
				B[i] -= (PartOfMatrix[i*N + Row] / Elememts[0])*Elememts[1];
			}
		}

		free(Elememts);
	}
}

void PrintMass()
{
	for (int i = 0; i < N; i++)
	{
		MPI_Barrier(MPI_COMM_WORLD);
		//Определяем узел на котором лежит данная строка
		int Node = i % Size;
		//Определяем строку в данном узле
		int Row = i / Size;

		if (Rank == Node)
		{
			printf("%d , %d \n", Node, Rank);
			for (int i = 0; i < N; i++)
				printf("%d ", PartOfMatrix[Row*N + i]);
			printf("\n");
		}
	}
}
int main(int* argc, char*** argv)
{
	MPI_Init(argc, argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &Rank);
	MPI_Comm_size(MPI_COMM_WORLD, &Size);
	srand(time(NULL)+ Rank);

	MPI_Barrier(MPI_COMM_WORLD);
	GeneratePartOfMatrix();

	MPI_Barrier(MPI_COMM_WORLD);
	GaussForward();
	PrintMass();
	MPI_Barrier(MPI_COMM_WORLD);
//	GaussBackward();
	//PrintMass();

	MPI_Finalize();
    return 0;
}

