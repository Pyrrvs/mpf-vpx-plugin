' MPF Controller VBScript Helper
' Converts MPF plugin Changed* object results into 2D variant arrays
' compatible with existing VPX table code.
'
' Usage: Add this line near the top of your table script:
'   ExecuteGlobal GetTextFile("mpf_controller.vbs")
'
' Then replace Controller.ChangedLamps with MPF_ChangedLamps(Controller), etc.

Function MPF_ChangedLamps(Controller)
    Dim r : Set r = Controller.ChangedLamps
    If r Is Nothing Then
        MPF_ChangedLamps = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedLamps = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedLamps = arr
End Function

Function MPF_ChangedSolenoids(Controller)
    Dim r : Set r = Controller.ChangedSolenoids
    If r Is Nothing Then
        MPF_ChangedSolenoids = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedSolenoids = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedSolenoids = arr
End Function

Function MPF_ChangedGIStrings(Controller)
    Dim r : Set r = Controller.ChangedGIStrings
    If r Is Nothing Then
        MPF_ChangedGIStrings = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedGIStrings = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedGIStrings = arr
End Function

Function MPF_ChangedLEDs(Controller)
    Dim r : Set r = Controller.ChangedLEDs
    If r Is Nothing Then
        MPF_ChangedLEDs = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedLEDs = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedLEDs = arr
End Function

Function MPF_ChangedBrightnessLEDs(Controller)
    Dim r : Set r = Controller.ChangedBrightnessLEDs
    If r Is Nothing Then
        MPF_ChangedBrightnessLEDs = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedBrightnessLEDs = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.Brightness(i)
    Next
    MPF_ChangedBrightnessLEDs = arr
End Function

Function MPF_ChangedFlashers(Controller)
    Dim r : Set r = Controller.ChangedFlashers
    If r Is Nothing Then
        MPF_ChangedFlashers = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedFlashers = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedFlashers = arr
End Function

Function MPF_HardwareRules(Controller)
    Dim r : Set r = Controller.HardwareRules
    If r Is Nothing Then
        MPF_HardwareRules = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_HardwareRules = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 2)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Switch(i)
        arr(i, 1) = r.Coil(i)
        arr(i, 2) = r.Hold(i)
    Next
    MPF_HardwareRules = arr
End Function
