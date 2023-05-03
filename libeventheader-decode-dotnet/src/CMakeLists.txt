# eventheader-decode-dotnet = eventheader-decode-dotnet.dll
add_library(eventheader-decode-dotnet SHARED
    EventEnumerator.cs
    EventHeader.cs
    EventInfo.cs
    EventItemInfo.cs
    EventUtility.cs)
set_property(
    TARGET eventheader-decode-dotnet
    PROPERTY VS_DOTNET_REFERENCES
    System)
install(TARGETS eventheader-decode-dotnet)
