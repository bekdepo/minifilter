#include "Operation.h"
#include "whiteName.h"
#include "common.h"

extern BOOLEAN service_enable;

extern PFLT_FILTER gFilterHandle;

extern NPAGED_LOOKASIDE_LIST Pre2PostContextList;

FLT_PREOP_CALLBACK_STATUS
SwapPreDirCtrlBuffers(
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __deref_out_opt PVOID *CompletionContext
    )
{
	return FLT_PREOP_SUCCESS_WITH_CALLBACK;
}


FLT_POSTOP_CALLBACK_STATUS
SwapPostDirCtrlBuffers(
    __inout PFLT_CALLBACK_DATA Data,
    __in PCFLT_RELATED_OBJECTS FltObjects,
    __in PVOID CompletionContext,
    __in FLT_POST_OPERATION_FLAGS Flags
    )
{
	ULONG nextOffset = 0;
	int modified = 0;
	int removedAllEntries = 1;  
	WCHAR *fullPathLongName;
	NTSTATUS status;
	PFLT_FILE_NAME_INFORMATION nameInfo;

	PFILE_BOTH_DIR_INFORMATION currentFileInfo = 0;     
	PFILE_BOTH_DIR_INFORMATION nextFileInfo = 0;    
	PFILE_BOTH_DIR_INFORMATION previousFileInfo = 0;    

	PFILE_ID_BOTH_DIR_INFORMATION currentFileIdInfo = 0;
	PFILE_ID_BOTH_DIR_INFORMATION nextFileIdInfo = 0;
	PFILE_ID_BOTH_DIR_INFORMATION previousFileIdInfo = 0;

	UNREFERENCED_PARAMETER( FltObjects );
	UNREFERENCED_PARAMETER( CompletionContext );   

	if( FlagOn( Flags, FLTFL_POST_OPERATION_DRAINING ) || 
		Data->Iopb->MinorFunction != IRP_MN_QUERY_DIRECTORY ||
		Data->Iopb->Parameters.DirectoryControl.QueryDirectory.Length <= 0 ||
		!NT_SUCCESS(Data->IoStatus.Status) || service_enable )
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}


	fullPathLongName = ExAllocatePool(NonPagedPool, _CMD_PATH*sizeof(WCHAR));
	if (fullPathLongName == NULL)
	{
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	RtlZeroMemory(fullPathLongName, 296*sizeof(WCHAR));

	status = FltGetFileNameInformation( Data,
		FLT_FILE_NAME_OPENED|FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP,
		&nameInfo );

	if (!NT_SUCCESS(status))
	{
		goto LAST_CODE;
	}


	FltParseFileNameInformation( nameInfo );

	status = FileMonGetFullPathName(nameInfo,fullPathLongName);
	if (!NT_SUCCESS(status))
	{
		goto LAST_CODE;
	}

	FltReleaseFileNameInformation( nameInfo );
	RemoveBacklash(fullPathLongName);


	//WindowsXP�������°汾����Ҫ���� FileBothDirectoryInformation ���͵���Ϣ 
	if(Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass == FileBothDirectoryInformation)
	{
		
		/*
		����õ�һ���������������������ͱ������ļ��������е��ļ���Ϣ��Ȼ�󣬸�����������
		�ṹ�����������˵�Ҫ���ص��ļ������ܴﵽ���ص�Ŀ���ˡ� 
		*/
		if (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress != NULL)
		{
			currentFileInfo=(PFILE_BOTH_DIR_INFORMATION)MmGetSystemAddressForMdlSafe( 
				Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
				NormalPagePriority );            
		}
		else
		{
			currentFileInfo=(PFILE_BOTH_DIR_INFORMATION)Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer;             
		}     

		if(currentFileInfo==NULL)return FLT_POSTOP_FINISHED_PROCESSING;       
		previousFileInfo = currentFileInfo;

		do
		{
			WCHAR *tempBuf;

			nextOffset = currentFileInfo->NextEntryOffset;//�õ���һ������ƫ�Ƶ�ַ
			nextFileInfo = (PFILE_BOTH_DIR_INFORMATION)((PCHAR)(currentFileInfo) + nextOffset); //��̽��ָ��          

			tempBuf = (WCHAR *)ExAllocatePool(NonPagedPool, _CMD_PATH*sizeof(WCHAR));

			if (tempBuf == NULL)
			{
				goto LAST_CODE;
			}

			RtlZeroMemory(tempBuf, _CMD_PATH*sizeof(WCHAR));
			RtlCopyMemory(tempBuf, currentFileInfo->FileName, currentFileInfo->FileNameLength);

			if (SearchIsProtect(fullPathLongName, tempBuf)) //������Ҫ�������ļ�
			{
				if( nextOffset == 0 )
				{
					previousFileInfo->NextEntryOffset = 0;
				}

				else//����ǰ�������ָ����һ����ƫ�������Թ�Ҫ���ص��ļ����ļ���㣬�ﵽ����Ŀ��
				{
					previousFileInfo->NextEntryOffset = (ULONG)((PCHAR)currentFileInfo - (PCHAR)previousFileInfo) + nextOffset;
				}
				modified = 1;
			}
			else
			{
				removedAllEntries = 0;
				previousFileInfo = currentFileInfo;  //ǰ�����ָ����� 
			}     
			currentFileInfo = nextFileInfo; //��ǰָ����� 

			if (tempBuf != NULL)
			{
				ExFreePool(tempBuf);
			}

		} while( nextOffset != 0 );
	}


	//
	//Windows Vista��Windows7����߰汾��Windows�Ĳ���ϵͳ��
	//���Ƿ��صĽṹ������FileBothDirectoryInformation. ����FileIdBothDirectoryInformation
	else if(Data->Iopb->Parameters.DirectoryControl.QueryDirectory.FileInformationClass ==FileIdBothDirectoryInformation)
	{	
		/*
		����õ�һ���������������������ͱ������ļ��������е��ļ���Ϣ��Ȼ��
		�����������Ľṹ�����������˵�Ҫ���ص��ļ������ܴﵽ���ص�Ŀ���ˡ� 
		*/
		if (Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress != NULL)
		{
			currentFileIdInfo=(PFILE_ID_BOTH_DIR_INFORMATION)MmGetSystemAddressForMdlSafe( 
				Data->Iopb->Parameters.DirectoryControl.QueryDirectory.MdlAddress,
				NormalPagePriority );            
		}
		else
		{
			currentFileIdInfo=(PFILE_ID_BOTH_DIR_INFORMATION)Data->Iopb->Parameters.DirectoryControl.QueryDirectory.DirectoryBuffer;             
		}     

		if(currentFileIdInfo==NULL)return FLT_POSTOP_FINISHED_PROCESSING;
		previousFileIdInfo = currentFileIdInfo;

		do
		{
			nextOffset = currentFileIdInfo->NextEntryOffset; //�õ���һ������ƫ�Ƶ�ַ   
			nextFileIdInfo = (PFILE_ID_BOTH_DIR_INFORMATION)((PCHAR)(currentFileIdInfo) + nextOffset);  //��̽��ָ��            

			if (SearchIsProtect(fullPathLongName, currentFileIdInfo->FileName))
			{
				if( nextOffset == 0 )
				{
					previousFileIdInfo->NextEntryOffset = 0;
				}
				else//����ǰ�������ָ����һ����ƫ�������Թ�Ҫ���ص��ļ����ļ���㣬�ﵽ����Ŀ��
				{
					previousFileIdInfo->NextEntryOffset = (ULONG)((PCHAR)currentFileIdInfo - (PCHAR)previousFileIdInfo) + nextOffset;
				}
				modified = 1;
			}
			else
			{
				removedAllEntries = 0;                
				previousFileIdInfo = currentFileIdInfo;                
			}
			currentFileIdInfo = nextFileIdInfo;

		} while( nextOffset != 0 );
	}


LAST_CODE:

	if( modified )
	{
		if( removedAllEntries )
		{
			Data->IoStatus.Status = STATUS_NO_MORE_FILES;
		}
		else
		{
			FltSetCallbackDataDirty( Data );
		}
	}   

	if (fullPathLongName)
	{
		ExFreePool(fullPathLongName);
	}

	DbgPrint(" Leave PtPostDirCtrlPassThrough()\n");


	return FLT_POSTOP_FINISHED_PROCESSING;
}