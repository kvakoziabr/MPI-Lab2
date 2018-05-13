// MPI_LR2.cpp: определяет точку входа для консольного приложения.
//

#include "stdafx.h"
#include "mpi.h"
#include <math.h>
#include <random>
#include <time.h>
#include <windows.h>


const int N = 100; //Размер матрицы
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
	for (int i = StartIndex; i < MatrixSize; i++)
	{
		for (int j = 0; j < N; j++)
		{
			if (PartOfMatrix[i*N + j] > Max)
			{
				Max = PartOfMatrix[i*N + j];
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
void SwapRows(int SouceRow, int SourceNode, int DestRow, int DestNode)
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
				Buf[i] = PartOfMatrix[SourceNode*N + i];
			}

			MPI_Send(Buf, N, MPI_INT, DestNode, 1, MPI_COMM_WORLD);
			MPI_Recv(Recv, N, MPI_INT, DestNode, 1, MPI_COMM_WORLD, MPI_STATUSES_IGNORE);

			for (int i = 0; i < N; i++)
			{
				PartOfMatrix[SourceNode*N + i] = Recv[i];
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
				Buf[i] = PartOfMatrix[SouceRow*N + i];
			}

			for (int i = 0; i < N; i++)
			{
				PartOfMatrix[SourceNode*N + i] = PartOfMatrix[DestRow*N + i];
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
		PartOfMatrix[i*N + SourceIndex] = PartOfMatrix[i*N + SourceIndex];
		PartOfMatrix[i*N + DestIndex] = Buf;
		Buf = NULL;
	}
}


//Прямой ход метода гаусса
void GaussForward()
{
	for (int i = 0; i < N; i++)
	{
		//Определяем узел на котором лежит данная строка
		int Node = (i + 1) % Size - 1;
		//Определяем строку в данном узле
		int Row = (i + 1) / Size;
		//Находим максимальный элемент во всей матрице
		FindMaxOfMatrix(Node,Row);
		//Меняем местами колонки
		SwapColumns(i,Maxi);
		//Меняем местами строки
		MPI_Barrier(MPI_COMM_WORLD);
		if (Rank == NodeWithMax || Rank == Node)
		SwapRows(Maxj, 1, Row, 1);
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

	MPI_Finalize();
    return 0;
}

