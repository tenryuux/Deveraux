// Projeto Deveraux - plugin DeverauxGroom: menu no Content Browser e comando de console para exportar grooms
//
// Clique direito num groom > Deveraux > Exportar para Alembic (Blender) ou (Unreal).
// Pelo console: Deveraux.ExportarGroom <groom> <arquivo.abc> [blender|unreal] [guias|semguias]

#include "DeverauxGroomExport.h"

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GroomAsset.h"
#include "HAL/IConsoleManager.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "DeverauxGroomEditor"

DEFINE_LOG_CATEGORY_STATIC(LogDeverauxGroom, Log, All);

namespace DeverauxGroomEditorPrivate
{
	using namespace DeverauxGroomExport;

	/** A ultima pasta usada no dialogo, para a proxima exportacao abrir nela. */
	FString LastDirectory;

	void Notify(const FString& Message, bool bSuccess)
	{
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = bSuccess ? 8.0f : 12.0f;
		Info.bUseSuccessFailIcons = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	const TCHAR* TargetName(ETarget Target)
	{
		return Target == ETarget::Blender ? TEXT("blender") : TEXT("unreal");
	}

	/** Exporta um groom e conta o resultado no Output Log e num aviso do editor. */
	bool ExportOne(UGroomAsset& Groom, const FString& Filename, const FOptions& Options)
	{
		FStats Stats;
		FString Error;
		if (!ExportGroom(Groom, Filename, Options, Stats, Error))
		{
			UE_LOG(LogDeverauxGroom, Error, TEXT("%s nao foi exportado: %s"), *Groom.GetName(), *Error);
			Notify(FString::Printf(TEXT("%s nao foi exportado: %s"), *Groom.GetName(), *Error), false);
			return false;
		}
		const FString Summary = FString::Printf(TEXT("%s: %d fios e %d guias em %d grupos (%lld pontos, %d objetos) em %s"), *Groom.GetName(),
			Stats.Strands, Stats.Guides, Stats.Groups, Stats.Points, Stats.Objects, *Filename);
		UE_LOG(LogDeverauxGroom, Display, TEXT("%s"), *Summary);
		if (Stats.SkippedGuides > 0)
		{
			UE_LOG(LogDeverauxGroom, Display, TEXT("%d guias ficaram fora (no Blender elas viram fios comuns). Para levar: guias no comando."),
				Stats.SkippedGuides);
		}
		if (!Stats.bWidths)
		{
			UE_LOG(LogDeverauxGroom, Warning, TEXT("%s nao tem largura nos fios: o arquivo vai sem largura."), *Groom.GetName());
		}
		Notify(Summary, true);
		return true;
	}

	void ExportSelected(ETarget Target)
	{
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> Selected;
		ContentBrowser.Get().GetSelectedAssets(Selected);
		IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
		if (!Desktop)
		{
			Notify(TEXT("O dialogo de salvar nao esta disponivel. Use o comando Deveraux.ExportarGroom."), false);
			return;
		}
		const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
		int32 NumGrooms = 0;
		for (const FAssetData& Asset : Selected)
		{
			UGroomAsset* Groom = Cast<UGroomAsset>(Asset.GetAsset());
			if (!Groom)
			{
				continue;
			}
			++NumGrooms;
			TArray<FString> Files;
			const FString Title = FString::Printf(TEXT("Exportar %s para Alembic (%s)"), *Groom->GetName(), TargetName(Target));
			const FString DefaultFile = FString::Printf(TEXT("%s_%s.abc"), *Groom->GetName(), TargetName(Target));
			if (!Desktop->SaveFileDialog(ParentWindow, Title, LastDirectory, DefaultFile, TEXT("Alembic (*.abc)|*.abc"), EFileDialogFlags::None,
				Files) || Files.Num() == 0)
			{
				continue;
			}
			FString Filename = FPaths::ConvertRelativePathToFull(Files[0]);
			if (!Filename.EndsWith(TEXT(".abc")))
			{
				Filename += TEXT(".abc");
			}
			LastDirectory = FPaths::GetPath(Filename);
			ExportOne(*Groom, Filename, DefaultOptions(Target));
		}
		if (NumGrooms == 0)
		{
			Notify(TEXT("Selecione um groom no Content Browser."), false);
		}
	}

	void ExportCommand(const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogDeverauxGroom, Display, TEXT("Uso: Deveraux.ExportarGroom <groom> <arquivo.abc> [blender|unreal] [guias|semguias]"));
			UE_LOG(LogDeverauxGroom, Display, TEXT("Exemplo: Deveraux.ExportarGroom /Game/Personagens/Cabelo C:/Exportados/Cabelo.abc blender"));
			return;
		}
		// O Copy Reference vem como /Script/HairStrandsCore.GroomAsset'/Game/Pasta/Cabelo.Cabelo': fica o que esta entre as aspas.
		// /Game/Pasta/Cabelo vira /Game/Pasta/Cabelo.Cabelo, o caminho do objeto.
		FString AssetPath = Args[0];
		int32 FirstQuote = INDEX_NONE;
		int32 LastQuote = INDEX_NONE;
		if (AssetPath.FindChar(TEXT('\''), FirstQuote) && AssetPath.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote + 1)
		{
			AssetPath = AssetPath.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
		}
		if (!FPaths::GetCleanFilename(AssetPath).Contains(TEXT(".")))
		{
			AssetPath += TEXT(".") + FPaths::GetBaseFilename(AssetPath);
		}
		UGroomAsset* Groom = LoadObject<UGroomAsset>(nullptr, *AssetPath);
		if (!Groom)
		{
			UE_LOG(LogDeverauxGroom, Error, TEXT("Nao achei o groom %s (clique direito nele > Copy Reference da o caminho)."), *Args[0]);
			return;
		}
		ETarget Target = ETarget::Blender;
		int32 Guides = INDEX_NONE;
		for (int32 Index = 2; Index < Args.Num(); ++Index)
		{
			const FString Option = Args[Index].ToLower();
			if (Option == TEXT("blender") || Option == TEXT("unreal"))
			{
				Target = Option == TEXT("unreal") ? ETarget::Unreal : ETarget::Blender;
			}
			else if (Option == TEXT("guias") || Option == TEXT("semguias"))
			{
				Guides = Option == TEXT("guias") ? 1 : 0;
			}
			else
			{
				UE_LOG(LogDeverauxGroom, Error, TEXT("Opcao desconhecida: %s (use blender, unreal, guias ou semguias)."), *Args[Index]);
				return;
			}
		}
		FOptions Options = DefaultOptions(Target);
		if (Guides != INDEX_NONE)
		{
			Options.bGuides = Guides == 1;
		}
		FString Filename = FPaths::ConvertRelativePathToFull(Args[1]);
		if (!Filename.EndsWith(TEXT(".abc")))
		{
			Filename += TEXT(".abc");
		}
		ExportOne(*Groom, Filename, Options);
	}
}

class FDeverauxGroomEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDeverauxGroomEditorModule::RegisterMenus));
		Command = IConsoleManager::Get().RegisterConsoleCommand(TEXT("Deveraux.ExportarGroom"),
			TEXT("Exporta um groom para Alembic: Deveraux.ExportarGroom <groom> <arquivo.abc> [blender|unreal] [guias|semguias]"),
			FConsoleCommandWithArgsDelegate::CreateStatic(&DeverauxGroomEditorPrivate::ExportCommand), ECVF_Default);
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		if (Command)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command);
			Command = nullptr;
		}
	}

private:
	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu.GroomAsset"));
		FToolMenuSection& Section = Menu->FindOrAddSection(TEXT("DeverauxGroom"), LOCTEXT("Secao", "Deveraux"));
		Section.AddMenuEntry(TEXT("DeverauxExportarBlender"),
			LOCTEXT("ExportarBlender", "Exportar para Alembic (Blender)"),
			LOCTEXT("ExportarBlenderDica", "Grava os fios de origem em metros, um objeto por grupo, sem as guias. Abra no Blender em File > Import > Alembic."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&DeverauxGroomEditorPrivate::ExportSelected, DeverauxGroomExport::ETarget::Blender)));
		Section.AddMenuEntry(TEXT("DeverauxExportarUnreal"),
			LOCTEXT("ExportarUnreal", "Exportar para Alembic (Unreal, cm)"),
			LOCTEXT("ExportarUnrealDica", "Grava em centimetros, com as guias e todos os atributos groom_*, para importar de volta na Unreal."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&DeverauxGroomEditorPrivate::ExportSelected, DeverauxGroomExport::ETarget::Unreal)));
	}

	IConsoleObject* Command = nullptr;
};

IMPLEMENT_MODULE(FDeverauxGroomEditorModule, DeverauxGroomEditor)

#undef LOCTEXT_NAMESPACE
