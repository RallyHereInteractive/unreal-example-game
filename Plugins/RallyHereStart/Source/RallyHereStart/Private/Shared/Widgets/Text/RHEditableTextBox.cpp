// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Shared/Widgets/Text/RHEditableTextBox.h"

TSharedRef<SWidget> URHEditableTextBox::RebuildWidget()
{
    Super::RebuildWidget();

    if (MyEditableTextBlock.IsValid())
    {
        MyEditableTextBlock->SetOnKeyDownHandler(BIND_UOBJECT_DELEGATE(FOnKeyDown, HandleOnKeyDown));
    }
    
    return MyEditableTextBlock.ToSharedRef();
}

FReply URHEditableTextBox::HandleOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    if (OnKeyDown.IsBound())
    {
        return OnKeyDown.Execute(MyGeometry, InKeyEvent).NativeReply;
    }
    return FReply::Unhandled();
}
