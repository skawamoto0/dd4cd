// dd4cd.cpp : コンソール アプリケーションのエントリ ポイントを定義します。
//

#include "stdafx.h"


#define BLOCK_UNREAD					0
#define BLOCK_READ						1
#define BLOCK_PENDING					2
#define BLOCK_FINAL						3
#define BLOCK_BAD						4

#define MIN_BLOCK_SIZE					4096
#define TIMEOUT_BAD						60000
#define TIMEOUT_PENDING					5000
#define TIMEOUT_SKIP					300

class CDriveDuplicator
{
public:
	HANDLE m_hIn;
	HANDLE m_hOut;
	HANDLE m_hLog;
	HANDLE m_hEventRead;
	HANDLE m_hEventWrite;
	ULONGLONG m_Size;
	ULONGLONG m_BlockSize;
	ULONGLONG m_BlockCount;
	BYTE* m_pBlockBuffer;
	BYTE* m_pBlockBitmap;
	ULONGLONG m_ReadCount;
	ULONGLONG m_PendingCount;
	ULONGLONG m_BadCount;
	CDriveDuplicator()
	{
		m_hIn = INVALID_HANDLE_VALUE;
		m_hOut = INVALID_HANDLE_VALUE;
		m_hLog = INVALID_HANDLE_VALUE;
		m_hEventRead = NULL;
		m_hEventWrite = NULL;
		m_Size = 0;
		m_BlockSize = 0;
		m_BlockCount = 0;
		m_pBlockBuffer = NULL;
		m_pBlockBitmap = NULL;
		m_ReadCount = 0;
		m_PendingCount = 0;
		m_BadCount = 0;
	}
	~CDriveDuplicator()
	{
		CloseDrive();
	}
	BOOL OpenDrive(LPCTSTR In, LPCTSTR Out, LPCTSTR Log, ULONGLONG BlockSize)
	{
		BOOL bResult;
		ULONGLONG SizeIn;
		ULONGLONG SizeOut;
		DWORD dw;
		bResult = FALSE;
		BlockSize = ((BlockSize + MIN_BLOCK_SIZE - 1) / MIN_BLOCK_SIZE) * MIN_BLOCK_SIZE;
		m_hIn = CreateFile(In, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH, NULL);
		if(m_hIn != INVALID_HANDLE_VALUE)
		{
			m_hOut = CreateFile(Out, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH, NULL);
			if(m_hOut == INVALID_HANDLE_VALUE)
				m_hOut = CreateFile(Out, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_FLAG_WRITE_THROUGH, NULL);
			if(m_hOut != INVALID_HANDLE_VALUE)
			{
				m_hLog = CreateFile(Log, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if(m_hLog != INVALID_HANDLE_VALUE)
				{
					m_hEventRead = CreateEvent(NULL, TRUE, FALSE, NULL);
					m_hEventWrite = CreateEvent(NULL, TRUE, FALSE, NULL);
					SizeIn = 0;
					if(!DeviceIoControl(m_hIn, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &SizeIn, sizeof(GET_LENGTH_INFORMATION), &dw, NULL))
						GetFileSizeEx(m_hIn, (PLARGE_INTEGER)&SizeIn);
					SizeOut = 0;
					if(!DeviceIoControl(m_hOut, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &SizeOut, sizeof(GET_LENGTH_INFORMATION), &dw, NULL))
						GetFileSizeEx(m_hOut, (PLARGE_INTEGER)&SizeOut);
					if(SizeIn > 0)
					{
						if(SizeOut < SizeIn)
						{
							if(SetFilePointerEx(m_hOut, *(LARGE_INTEGER*)&SizeIn, NULL, FILE_BEGIN))
							{
								SetEndOfFile(m_hOut);
								SetFilePointer(m_hOut, 0, NULL, FILE_BEGIN);
								GetFileSizeEx(m_hOut, (PLARGE_INTEGER)&SizeOut);
							}
						}
						if(SizeOut >= SizeIn)
						{
							if(ReadLog())
							{
								if(m_Size == SizeIn)
								{
									if(m_BlockSize == BlockSize)
										bResult = TRUE;
									else
									{
										if(ConvertBlockSize(BlockSize))
											bResult = TRUE;
									}
								}
							}
							else
							{
								m_Size = SizeIn;
								m_BlockSize = BlockSize;
								m_BlockCount = (m_Size + m_BlockSize - 1) / m_BlockSize;
								m_pBlockBuffer = (BYTE*)VirtualAlloc(NULL, (SIZE_T)m_BlockSize, MEM_COMMIT, PAGE_READWRITE);
								if(m_pBlockBuffer != NULL)
								{
									memset(m_pBlockBuffer, 0, (size_t)m_BlockSize);
									m_pBlockBitmap = (BYTE*)VirtualAlloc(NULL, (SIZE_T)m_BlockCount, MEM_COMMIT, PAGE_READWRITE);
									if(m_pBlockBitmap != NULL)
									{
										memset(m_pBlockBitmap, BLOCK_UNREAD, (size_t)m_BlockCount);
										m_ReadCount = 0;
										m_PendingCount = 0;
										m_BadCount = 0;
										m_pBlockBitmap[0] = BLOCK_FINAL;
										bResult = TRUE;
									}
								}
							}
						}
					}
				}
			}
		}
		return bResult;
	}
	void CloseDrive()
	{
		WriteLog();
		if(m_hIn != INVALID_HANDLE_VALUE)
			CloseHandle(m_hIn);
		m_hIn = INVALID_HANDLE_VALUE;
		if(m_hOut != INVALID_HANDLE_VALUE)
			CloseHandle(m_hOut);
		m_hOut = INVALID_HANDLE_VALUE;
		if(m_hLog != INVALID_HANDLE_VALUE)
			CloseHandle(m_hLog);
		m_hLog = INVALID_HANDLE_VALUE;
		if(m_hEventRead != NULL)
			CloseHandle(m_hEventRead);
		m_hEventRead = NULL;
		if(m_hEventWrite != NULL)
			CloseHandle(m_hEventWrite);
		m_hEventWrite = NULL;
		m_Size = 0;
		m_BlockSize = 0;
		m_BlockCount = 0;
		if(m_pBlockBuffer != NULL)
			VirtualFree(m_pBlockBuffer, 0, MEM_RELEASE);
		m_pBlockBuffer = NULL;
		if(m_pBlockBitmap != NULL)
			VirtualFree(m_pBlockBitmap, 0, MEM_RELEASE);
		m_pBlockBitmap = NULL;
		m_ReadCount = 0;
		m_PendingCount = 0;
		m_BadCount = 0;
	}
	BOOL ReadLog()
	{
		BOOL bResult;
		DWORD dw;
		ULONGLONG i;
		bResult = FALSE;
		SetFilePointer(m_hLog, 0, NULL, FILE_BEGIN);
		if(ReadFile(m_hLog, &m_Size, sizeof(ULONGLONG), &dw, NULL))
		{
			if(ReadFile(m_hLog, &m_BlockSize, sizeof(ULONGLONG), &dw, NULL))
			{
				if(ReadFile(m_hLog, &m_BlockCount, sizeof(ULONGLONG), &dw, NULL))
				{
					m_pBlockBuffer = (BYTE*)VirtualAlloc(NULL, (SIZE_T)m_BlockSize, MEM_COMMIT, PAGE_READWRITE);
					if(m_pBlockBuffer != NULL)
					{
						m_pBlockBitmap = (BYTE*)VirtualAlloc(NULL, (SIZE_T)m_BlockCount, MEM_COMMIT, PAGE_READWRITE);
						if(m_pBlockBitmap != NULL)
						{
							if(ReadFile(m_hLog, m_pBlockBitmap, (DWORD)m_BlockCount, &dw, NULL))
							{
								m_ReadCount = 0;
								m_PendingCount = 0;
								m_BadCount = 0;
								for(i = 0; i < m_BlockCount; i++)
								{
									if(m_pBlockBitmap[i] == BLOCK_READ)
										m_ReadCount++;
									else if(m_pBlockBitmap[i] == BLOCK_PENDING)
										m_PendingCount++;
									else if(m_pBlockBitmap[i] == BLOCK_BAD)
										m_BadCount++;
								}
								if(m_ReadCount + m_PendingCount + m_BadCount == m_BlockCount)
								{
									for(i = 0; i < m_BlockCount; i++)
									{
										if(m_pBlockBitmap[i] == BLOCK_BAD)
											m_pBlockBitmap[i] = BLOCK_PENDING;
									}
									m_PendingCount += m_BadCount;
									m_BadCount = 0;
								}
								bResult = TRUE;
							}
						}
					}
				}
			}
		}
		return bResult;
	}
	BOOL WriteLog()
	{
		BOOL bResult;
		DWORD dw;
		bResult = FALSE;
		SetFilePointer(m_hLog, 0, NULL, FILE_BEGIN);
		if(WriteFile(m_hLog, &m_Size, sizeof(ULONGLONG), &dw, NULL))
		{
			if(WriteFile(m_hLog, &m_BlockSize, sizeof(ULONGLONG), &dw, NULL))
			{
				if(WriteFile(m_hLog, &m_BlockCount, sizeof(ULONGLONG), &dw, NULL))
				{
					if(WriteFile(m_hLog, m_pBlockBitmap, (DWORD)m_BlockCount, &dw, NULL))
						bResult = TRUE;
				}
			}
		}
		return bResult;
	}
	BOOL FindNextBlockHead(ULONGLONG* Pointer, BYTE Block)
	{
		BOOL bResult;
		bResult = FALSE;
		while(*Pointer < m_BlockCount)
		{
			if(m_pBlockBitmap[*Pointer] == Block)
			{
				bResult = TRUE;
				break;
			}
			(*Pointer)++;
		}
		if(*Pointer == m_BlockCount)
		{
			*Pointer = 0;
			while(*Pointer < m_BlockCount)
			{
				if(m_pBlockBitmap[*Pointer] == Block)
				{
					bResult = TRUE;
					break;
				}
				(*Pointer)++;
			}
		}
		return bResult;
	}
	BOOL FindNextBlockCenter(ULONGLONG* Pointer, BYTE Block)
	{
		BOOL bResult;
		ULONGLONG End;
		bResult = FALSE;
		if(FindNextBlockHead(Pointer, Block))
		{
			End = *Pointer;
			while(End < m_BlockCount)
			{
				if(m_pBlockBitmap[End] != Block)
					break;
				End++;
			}
			*Pointer = (*Pointer + End) / 2;
			bResult = TRUE;
		}
		return bResult;
	}
	BOOL FindNextBlockCenter(ULONGLONG* Pointer, BYTE Block, ULONGLONG Minimum)
	{
		BOOL bResult;
		ULONGLONG Old;
		ULONGLONG End;
		bResult = FALSE;
		Old = *Pointer;
		while(*Pointer < m_BlockCount)
		{
			if(m_pBlockBitmap[*Pointer] == Block)
			{
				End = *Pointer;
				while(End < m_BlockCount)
				{
					if(m_pBlockBitmap[End] != Block)
						break;
					End++;
				}
				if(End - *Pointer >= Minimum)
				{
					*Pointer = (*Pointer + End) / 2;
					bResult = TRUE;
					break;
				}
				*Pointer = End - 1;
			}
			(*Pointer)++;
		}
		if(*Pointer == m_BlockCount)
		{
			*Pointer = 0;
			while(*Pointer < m_BlockCount)
			{
				if(m_pBlockBitmap[*Pointer] == Block)
				{
					End = *Pointer;
					while(End < m_BlockCount)
					{
						if(m_pBlockBitmap[End] != Block)
							break;
						End++;
					}
					if(End - *Pointer >= Minimum)
					{
						*Pointer = (*Pointer + End) / 2;
						bResult = TRUE;
						break;
					}
					*Pointer = End - 1;
				}
				(*Pointer)++;
			}
		}
		return bResult;
	}
	BOOL FindNextBlockHeadOrCenter(ULONGLONG* Pointer, BYTE Block, ULONGLONG Minimum)
	{
		BOOL bResult;
		bResult = FALSE;
		if(*Pointer < m_BlockCount)
		{
			if(m_pBlockBitmap[*Pointer] == Block)
				bResult = TRUE;
		}
		if(!bResult)
		{
			(*Pointer)++;
			if(*Pointer < m_BlockCount)
			{
				if(m_pBlockBitmap[*Pointer] == Block)
					bResult = TRUE;
			}
			else
				*Pointer = 0;
			if(!bResult)
			{
				if(FindNextBlockCenter(Pointer, Block, Minimum))
					bResult = TRUE;
			}
		}
		return bResult;
	}
	BOOL SetBlock(ULONGLONG* Pointer, BYTE Block)
	{
		BOOL bResult;
		bResult = FALSE;
		if(*Pointer < m_BlockCount)
		{
			if(m_pBlockBitmap[*Pointer] == BLOCK_READ)
				m_ReadCount--;
			else if(m_pBlockBitmap[*Pointer] == BLOCK_PENDING)
				m_PendingCount--;
			else if(m_pBlockBitmap[*Pointer] == BLOCK_BAD)
				m_BadCount--;
			if(Block == BLOCK_READ)
				m_ReadCount++;
			else if(Block == BLOCK_PENDING)
				m_PendingCount++;
			else if(Block == BLOCK_BAD)
				m_BadCount++;
			m_pBlockBitmap[*Pointer] = Block;
			bResult = TRUE;
		}
		return bResult;
	}
	BOOL ConvertBlockSize(ULONGLONG BlockSize)
	{
		BOOL bResult;
		ULONGLONG NewBlockCount;
		BYTE* NewBlockBuffer;
		BYTE* NewBlockBitmap;
		ULONGLONG i;
		ULONGLONG j;
		bResult = FALSE;
		BlockSize = ((BlockSize + MIN_BLOCK_SIZE - 1) / MIN_BLOCK_SIZE) * MIN_BLOCK_SIZE;
		NewBlockCount = (m_Size + BlockSize - 1) / BlockSize;
		NewBlockBuffer = (BYTE*)VirtualAlloc(NULL, (SIZE_T)BlockSize, MEM_COMMIT, PAGE_READWRITE);
		if(NewBlockBuffer != NULL)
		{
			NewBlockBitmap = (BYTE*)VirtualAlloc(NULL, (SIZE_T)NewBlockCount, MEM_COMMIT, PAGE_READWRITE);
			if(NewBlockBitmap != NULL)
			{
				for(i = 0; i < NewBlockCount; i++)
				{
					NewBlockBitmap[i] = BLOCK_READ;
					for(j = i * BlockSize / m_BlockSize; j < (i * BlockSize + (BlockSize - 1)) / m_BlockSize + 1; j++)
					{
						if(j < m_BlockCount)
						{
							if(m_pBlockBitmap[j] == BLOCK_FINAL)
								NewBlockBitmap[i] = BLOCK_FINAL;
							else if(m_pBlockBitmap[j] == BLOCK_UNREAD && NewBlockBitmap[i] != BLOCK_FINAL)
								NewBlockBitmap[i] = BLOCK_UNREAD;
							else if(m_pBlockBitmap[j] == BLOCK_PENDING && NewBlockBitmap[i] != BLOCK_UNREAD && NewBlockBitmap[i] != BLOCK_FINAL)
								NewBlockBitmap[i] = BLOCK_PENDING;
							else if(m_pBlockBitmap[j] == BLOCK_BAD && NewBlockBitmap[i] == BLOCK_READ)
								NewBlockBitmap[i] = BLOCK_BAD;
						}
					}
				}
				VirtualFree(m_pBlockBuffer, 0, MEM_RELEASE);
				VirtualFree(m_pBlockBitmap, 0, MEM_RELEASE);
				m_BlockSize = BlockSize;
				m_BlockCount = NewBlockCount;
				m_pBlockBuffer = NewBlockBuffer;
				m_pBlockBitmap = NewBlockBitmap;
				m_ReadCount = 0;
				m_PendingCount = 0;
				m_BadCount = 0;
				for(i = 0; i < m_BlockCount; i++)
				{
					if(m_pBlockBitmap[i] == BLOCK_READ)
						m_ReadCount++;
					else if(m_pBlockBitmap[i] == BLOCK_PENDING)
						m_PendingCount++;
					else if(m_pBlockBitmap[i] == BLOCK_BAD)
						m_BadCount++;
				}
				bResult = TRUE;
			}
		}
		return bResult;
	}
	BOOL IsCompleted()
	{
		BOOL bResult;
		bResult = FALSE;
		if(m_ReadCount + m_BadCount == m_BlockCount)
			bResult = TRUE;
		return bResult;
	}
	BOOL CopyBytes(ULONGLONG Address, ULONGLONG BlockSize, DWORD Timeout, BOOL Reload)
	{
		BOOL bResult;
		OVERLAPPED Overlapped;
		DWORD ReadByte;
		DWORD WriteByte;
		bResult = FALSE;
		memset(m_pBlockBuffer, 0, (size_t)BlockSize);
		if(Reload)
		{
			memset(&Overlapped, 0, sizeof(OVERLAPPED));
			memcpy(&Overlapped.Offset, &Address, sizeof(ULONGLONG));
			Overlapped.hEvent = m_hEventRead;
			ReadByte = 0;
			ReadFile(m_hOut, m_pBlockBuffer, (DWORD)BlockSize, NULL, &Overlapped);
			if(!GetOverlappedResultEx(m_hOut, &Overlapped, &ReadByte, Timeout, FALSE))
			{
				CancelIoEx(m_hOut, &Overlapped);
				GetOverlappedResultEx(m_hOut, &Overlapped, &ReadByte, INFINITE, FALSE);
			}
		}
		memset(&Overlapped, 0, sizeof(OVERLAPPED));
		memcpy(&Overlapped.Offset, &Address, sizeof(ULONGLONG));
		Overlapped.hEvent = m_hEventRead;
		ReadByte = 0;
		ReadFile(m_hIn, m_pBlockBuffer, (DWORD)BlockSize, NULL, &Overlapped);
		if(!GetOverlappedResultEx(m_hIn, &Overlapped, &ReadByte, Timeout, FALSE))
		{
			CancelIoEx(m_hIn, &Overlapped);
			GetOverlappedResultEx(m_hIn, &Overlapped, &ReadByte, INFINITE, FALSE);
		}
		memset(&Overlapped, 0, sizeof(OVERLAPPED));
		memcpy(&Overlapped.Offset, &Address, sizeof(ULONGLONG));
		Overlapped.hEvent = m_hEventWrite;
		WriteByte = 0;
		WriteFile(m_hOut, m_pBlockBuffer, (DWORD)BlockSize, NULL, &Overlapped);
		if(!GetOverlappedResultEx(m_hOut, &Overlapped, &WriteByte, Timeout, FALSE))
		{
			CancelIoEx(m_hOut, &Overlapped);
			GetOverlappedResultEx(m_hOut, &Overlapped, &WriteByte, INFINITE, FALSE);
		}
		if(ReadByte == BlockSize && WriteByte == BlockSize)
			bResult = TRUE;
		return bResult;
	}
	BOOL CopyBlock(ULONGLONG* Pointer, ULONGLONG* Minimum)
	{
		BOOL bResult;
		ULONGLONG Address;
		ULONGLONG BlockSize;
		DWORD Time;
		BOOL Copy;
		bResult = FALSE;
		if(*Pointer < m_BlockCount)
		{
			Address = *Pointer * m_BlockSize;
			BlockSize = min(m_BlockSize, m_Size - Address);
			memset(m_pBlockBuffer, 0, (size_t)BlockSize);
			Time = timeGetTime();
			if(m_pBlockBitmap[*Pointer] == BLOCK_PENDING)
				Copy = CopyBytes(Address, BlockSize, TIMEOUT_BAD, TRUE);
			else
				Copy = CopyBytes(Address, BlockSize, TIMEOUT_PENDING, FALSE);
			Time = timeGetTime() - Time;
			if(Copy)
			{
				if(m_pBlockBitmap[*Pointer] == BLOCK_PENDING)
				{
					SetBlock(Pointer, BLOCK_READ);
					FindNextBlockHead(Pointer, BLOCK_PENDING);
				}
				else if(Time > TIMEOUT_SKIP)
				{
					SetBlock(Pointer, BLOCK_READ);
					while(!FindNextBlockCenter(Pointer, BLOCK_UNREAD, *Minimum))
					{
						*Minimum /= 2;
						if(*Minimum <= 1)
							break;
					}
					FindNextBlockHead(Pointer, BLOCK_UNREAD);
				}
				else
				{
					SetBlock(Pointer, BLOCK_READ);
					FindNextBlockHead(Pointer, BLOCK_UNREAD);
				}
			}
			else
			{
				if(m_pBlockBitmap[*Pointer] == BLOCK_PENDING)
				{
					SetBlock(Pointer, BLOCK_BAD);
					FindNextBlockHead(Pointer, BLOCK_PENDING);
				}
				else
				{
					SetBlock(Pointer, BLOCK_PENDING);
					while(!FindNextBlockCenter(Pointer, BLOCK_UNREAD, *Minimum))
					{
						*Minimum /= 2;
						if(*Minimum <= 1)
							break;
					}
					FindNextBlockHead(Pointer, BLOCK_UNREAD);
				}
			}
			if(*Pointer >= m_BlockCount)
				FindNextBlockHead(Pointer, BLOCK_PENDING);
			if(*Pointer >= m_BlockCount)
				FindNextBlockHead(Pointer, BLOCK_FINAL);
			bResult = TRUE;
		}
		return bResult;
	}
};

HANDLE g_hConsole;
BOOL g_bAborted;
CDriveDuplicator g_Duplicator;
ULONGLONG g_Pointer;
ULONGLONG g_Minimum;

BOOL WINAPI HandlerRoutine(DWORD dwCtrlType)
{
	g_bAborted = TRUE;
	return TRUE;
}

void UpdateProgress()
{
	CONSOLE_SCREEN_BUFFER_INFO Info;
	ULONGLONG i;
	GetConsoleScreenBufferInfo(g_hConsole, &Info);
	Info.dwCursorPosition.X = 0;
	Info.dwCursorPosition.Y -= 6;
	SetConsoleCursorPosition(g_hConsole, Info.dwCursorPosition);
	_tcprintf(_T("Read blocks     %16llu\n"), g_Duplicator.m_ReadCount);
	_tcprintf(_T("Pending blocks  %16llu\n"), g_Duplicator.m_PendingCount);
	_tcprintf(_T("Bad blocks      %16llu\n"), g_Duplicator.m_BadCount);
	_tcprintf(_T("Progress        %u%%\n"), (DWORD)(100 * (g_Duplicator.m_ReadCount + g_Duplicator.m_BadCount) / g_Duplicator.m_BlockCount));
	_tcprintf(_T("Current block   %16llu\n"), g_Pointer);
	for(i = 0; i < 40; i++)
	{
		if(i == 20)
			_puttch(_T('*'));
		else if(g_Pointer + i >= 20 && g_Pointer + i < g_Duplicator.m_BlockCount + 20)
		{
			if(g_Duplicator.m_pBlockBitmap[g_Pointer + i - 20] == BLOCK_READ)
				_puttch(_T('O'));
			else if(g_Duplicator.m_pBlockBitmap[g_Pointer + i - 20] == BLOCK_PENDING)
				_puttch(_T('/'));
			else if(g_Duplicator.m_pBlockBitmap[g_Pointer + i - 20] == BLOCK_BAD)
				_puttch(_T('X'));
			else
				_puttch(_T('-'));
		}
		else
			_puttch(_T('_'));
	}
	_puttch(_T('\n'));
}

int _tmain(int argc, _TCHAR* argv[])
{
	DWORD Time;
	DWORD Time1;
	_putts(_T("Data Duplicator for Corrupted Drives"));
	_putts(_T("Copyright (C) 2021 Suguru Kawamoto"));
	_putts(_T(""));
	_putts(_T("Usage"));
	_putts(_T("dd4cd.exe in out log block"));
	_putts(_T("in      Path to the source drive"));
	_putts(_T("out     Path to the destination drive or an image file to save"));
	_putts(_T("log     Path to the log file"));
	_putts(_T("block   Block size of read and write in byte"));
	_putts(_T(""));
	_putts(_T("Example"));
	_putts(_T("dd4cd.exe \\\\.\\PhysicalDrive1 C:\\file.img C:\\file.log 4096"));
	_putts(_T(""));
	g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	g_bAborted = FALSE;
	SetConsoleCtrlHandler(HandlerRoutine, TRUE);
	if(argc == 5)
	{
		if(g_Duplicator.OpenDrive(argv[1], argv[2], argv[3], _tcstoul(argv[4], NULL, 0)))
		{
			g_Pointer = 0;
			g_Minimum = g_Duplicator.m_BlockCount / 2;
			if(!g_Duplicator.FindNextBlockHead(&g_Pointer, BLOCK_UNREAD))
			{
				if(!g_Duplicator.FindNextBlockHead(&g_Pointer, BLOCK_PENDING))
					g_Duplicator.FindNextBlockHead(&g_Pointer, BLOCK_FINAL);
			}
			_tcprintf(_T("Total blocks    %16llu\n"), g_Duplicator.m_BlockCount);
			_tcprintf(_T("\n"));
			_tcprintf(_T("\n"));
			_tcprintf(_T("\n"));
			_tcprintf(_T("\n"));
			_tcprintf(_T("\n"));
			_tcprintf(_T("\n"));
			Time = 0;
			Time1 = 0;
			while(!g_bAborted && !g_Duplicator.IsCompleted())
			{
				if(timeGetTime() - Time >= 1000)
				{
					UpdateProgress();
					Time = timeGetTime();
				}
				if(timeGetTime() - Time1 >= 300000)
				{
					g_Duplicator.WriteLog();
					Time1 = timeGetTime();
				}
				g_Duplicator.CopyBlock(&g_Pointer, &g_Minimum);
			}
			UpdateProgress();
			g_Duplicator.CloseDrive();
			if(g_bAborted)
			{
				_putts(_T("Aborted."));
				_putts(_T("The job can be resumed later with specifying the same log file."));
			}
			else
			{
				_putts(_T("Finished.\n"));
				if(g_Duplicator.m_BadCount > 0)
					_putts(_T("Run again with specifying the same log file to retry to read bad blocks."));
			}
		}
		else
			_putts(_T("Failed in opening the drives or the drives and the log file are mismatched."));
	}
	else
		_putts(_T("Invalid arguments."));
	SetConsoleCtrlHandler(NULL, FALSE);
	CloseHandle(g_hConsole);
	return 0;
}

