Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")

' Path to your list of EXEs
listPath = "batch_execute_EdgeMafia.txt"

If Not fso.FileExists(listPath) Then
    MsgBox "List file not found: " & listPath
    WScript.Quit
End If

Set file = fso.OpenTextFile(listPath, 1)

Do Until file.AtEndOfStream
    exePath = Trim(file.ReadLine)
    If exePath <> "" Then

        ' Launch the EXE
        shell.Run """" & exePath & """"
        WScript.Sleep 800   ' wait for window to appear

        ' Send 500 + Enter
        shell.SendKeys "500"
        shell.SendKeys "{ENTER}"
        WScript.Sleep 200

        ' Send 0 + Enter
        shell.SendKeys "0"
        shell.SendKeys "{ENTER}"
        WScript.Sleep 200

    End If
Loop

file.Close
