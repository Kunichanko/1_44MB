Randomize
Set fso = CreateObject("Scripting.FileSystemObject")
request = fso.BuildPath(fso.GetParentFolderName(WScript.ScriptFullName), "zipper_command.request")
If fso.FileExists(request) Then fso.DeleteFile request, True
Set output = fso.CreateTextFile(request, True)
output.WriteLine "eat " & Timer & "_" & Rnd & "_" & Rnd
output.Close
