#include "Presentation/CinderLANDiscovery.h"

#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

#pragma push_macro("FVector")
#define FVector FVectorWorkaround
#include "Apple/PreAppleSystemHeaders.h"
#import <Foundation/Foundation.h>
#import <arpa/inet.h>
#import <netinet/in.h>
#include "Apple/PostAppleSystemHeaders.h"
#undef FVector
#pragma pop_macro("FVector")

bool CinderLANParseProtocol(const FString& Value, int32& OutVersion);

namespace
{
constexpr int32 MaxResolvingServices = 32;
constexpr int32 MaxResolvedServices = 64;

FString BoundedMetadata(NSString* Value, int32 MaxCharacters)
{
    if (!Value) return FString();
    const FString Source(Value);
    FString Result;
    Result.Reserve(FMath::Min(Source.Len(), MaxCharacters));
    for (const TCHAR Character : Source)
    {
        if (Result.Len() >= MaxCharacters) break;
        if (Character >= TEXT(' ') && Character != 0x7f)
        {
            Result.AppendChar(Character);
        }
    }
    return Result;
}

FString ServiceId(NSNetService* Service)
{
    FString Id = FString::Printf(TEXT("%s|%s|%s"),
        *BoundedMetadata(Service.name, 64),
        *BoundedMetadata(Service.type, 64),
        *BoundedMetadata(Service.domain, 128));
    Id.ToLowerInline();
    return Id;
}

int32 IPv4Rank(uint32 HostAddress)
{
    const uint32 LastOctet = HostAddress & 0xffU;
    if (HostAddress == 0 || LastOctet == 0 || LastOctet == 255 ||
        (HostAddress & 0xf0000000U) == 0xe0000000U ||
        (HostAddress & 0xff000000U) == 0x7f000000U)
    {
        return -1;
    }
    if ((HostAddress & 0xff000000U) == 0x0a000000U ||
        (HostAddress & 0xfff00000U) == 0xac100000U ||
        (HostAddress & 0xffff0000U) == 0xc0a80000U)
    {
        return 0; // Private LAN ranges.
    }
    if ((HostAddress & 0xffff0000U) == 0xa9fe0000U)
    {
        return 1; // IPv4 link-local.
    }
    return -1;
}

FString PreferredIPv4(NSNetService* Service)
{
    FString Best;
    int32 BestRank = MAX_int32;
    for (NSData* AddressData in Service.addresses)
    {
        if ([AddressData length] < sizeof(sockaddr_in)) continue;
        const sockaddr* SocketAddress = static_cast<const sockaddr*>([AddressData bytes]);
        if (!SocketAddress || SocketAddress->sa_family != AF_INET) continue;
        const sockaddr_in* IPv4 = reinterpret_cast<const sockaddr_in*>(SocketAddress);
        const int32 Rank = IPv4Rank(ntohl(IPv4->sin_addr.s_addr));
        if (Rank < 0 || Rank >= BestRank) continue;
        char Buffer[INET_ADDRSTRLEN] = {};
        if (!inet_ntop(AF_INET, &IPv4->sin_addr, Buffer, sizeof(Buffer))) continue;
        Best = FString(UTF8_TO_TCHAR(Buffer));
        BestRank = Rank;
    }
    return Best;
}

FString ProtocolTXT(NSNetService* Service)
{
    NSData* TXTData = Service.TXTRecordData;
    if (!TXTData) return FString();
    NSDictionary<NSString*, NSData*>* TXT = [NSNetService dictionaryFromTXTRecordData:TXTData];
    for (NSString* Key in TXT)
    {
        if ([Key caseInsensitiveCompare:@"protocol"] != NSOrderedSame) continue;
        NSData* ValueData = [TXT objectForKey:Key];
        NSString* Value = [[[NSString alloc] initWithData:ValueData encoding:NSUTF8StringEncoding] autorelease];
        return Value ? BoundedMetadata(Value, 16) : FString();
    }
    return FString();
}
}

@interface FCinderLANBrowserBridge : NSObject <NSNetServiceBrowserDelegate, NSNetServiceDelegate>
{
@public
    FCriticalSection StateMutex;
    TMap<FString, FCinderLANService> Entries;
    FString StatusText;
    FString ErrorText;
    bool bRunning;
    uint64 Generation;

@private
    NSNetServiceBrowser* Browser;
    NSMutableDictionary<NSString*, NSNetService*>* Resolving;
    uint64 BrowserGeneration;
}
- (void)requestStart;
- (void)requestStop;
- (void)startOnMainThread:(uint64)ExpectedGeneration;
- (void)stopOnMainThread;
- (void)stopOnMainThreadIfCurrent:(uint64)ExpectedGeneration;
- (void)invalidate;
- (BOOL)isCurrentBrowser:(NSNetServiceBrowser*)Candidate;
- (void)publishStatus;
@end

@implementation FCinderLANBrowserBridge

- (instancetype)init
{
    self = [super init];
    if (self)
    {
        bRunning = false;
        Generation = 0;
        Browser = nil;
        BrowserGeneration = 0;
        Resolving = [[NSMutableDictionary alloc] init];
    }
    return self;
}

- (void)dealloc
{
    [self stopOnMainThread];
    [Resolving release];
    [super dealloc];
}

- (void)requestStart
{
    uint64 RequestedGeneration = 0;
    {
        FScopeLock Lock(&StateMutex);
        bRunning = true;
        RequestedGeneration = ++Generation;
        Entries.Empty();
        ErrorText.Empty();
        StatusText = TEXT("Looking for local games...");
    }
    FCinderLANBrowserBridge* StrongSelf = [self retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [StrongSelf startOnMainThread:RequestedGeneration];
        [StrongSelf release];
    });
}

- (void)requestStop
{
    uint64 RequestedGeneration = 0;
    {
        FScopeLock Lock(&StateMutex);
        bRunning = false;
        RequestedGeneration = ++Generation;
        Entries.Empty();
        ErrorText.Empty();
        StatusText.Empty();
    }
    FCinderLANBrowserBridge* StrongSelf = [self retain];
    dispatch_async(dispatch_get_main_queue(), ^{
        [StrongSelf stopOnMainThreadIfCurrent:RequestedGeneration];
        [StrongSelf release];
    });
}

- (void)startOnMainThread:(uint64)ExpectedGeneration
{
    {
        FScopeLock Lock(&StateMutex);
        if (!bRunning || Generation != ExpectedGeneration) return;
    }
    [self stopOnMainThread];
    {
        FScopeLock Lock(&StateMutex);
        if (!bRunning || Generation != ExpectedGeneration) return;
    }
    Browser = [[NSNetServiceBrowser alloc] init];
    Browser.delegate = self;
    [Browser scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    bool bStillCurrent = false;
    {
        FScopeLock Lock(&StateMutex);
        bStillCurrent = bRunning && Generation == ExpectedGeneration;
        if (bStillCurrent) BrowserGeneration = ExpectedGeneration;
    }
    if (!bStillCurrent)
    {
        [self stopOnMainThread];
        return;
    }
    [Browser searchForServicesOfType:@"_cinderline._tcp" inDomain:@"local."];
}

- (void)stopOnMainThreadIfCurrent:(uint64)ExpectedGeneration
{
    {
        FScopeLock Lock(&StateMutex);
        if (bRunning || Generation != ExpectedGeneration) return;
    }
    [self stopOnMainThread];
}

- (void)invalidate
{
    FScopeLock Lock(&StateMutex);
    bRunning = false;
    ++Generation;
    Entries.Empty();
    ErrorText.Empty();
    StatusText.Empty();
}

- (void)stopOnMainThread
{
    if (Browser)
    {
        Browser.delegate = nil;
        [Browser stop];
        [Browser removeFromRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        [Browser release];
        Browser = nil;
        FScopeLock Lock(&StateMutex);
        BrowserGeneration = 0;
    }
    for (NSNetService* Service in [Resolving allValues])
    {
        Service.delegate = nil;
        [Service stop];
        [Service removeFromRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    }
    [Resolving removeAllObjects];
}

- (BOOL)isCurrentBrowser:(NSNetServiceBrowser*)Candidate
{
    FScopeLock Lock(&StateMutex);
    return bRunning && Candidate == Browser && BrowserGeneration == Generation;
}

- (void)publishStatus
{
    FScopeLock Lock(&StateMutex);
    if (!bRunning) return;
    int32 Compatible = 0;
    for (const TPair<FString, FCinderLANService>& Pair : Entries)
    {
        Compatible += Pair.Value.bCompatible ? 1 : 0;
    }
    if (Compatible > 0)
    {
        StatusText = FString::Printf(TEXT("Found %d local %s."), Compatible, Compatible == 1 ? TEXT("game") : TEXT("games"));
    }
    else if (!Entries.IsEmpty())
    {
        StatusText = TEXT("Local games were found, but they use a different protocol version.");
    }
    else if (bRunning)
    {
        StatusText = TEXT("Looking for local games...");
    }
}

- (void)netServiceBrowserWillSearch:(NSNetServiceBrowser*)InBrowser
{
    FScopeLock Lock(&StateMutex);
    if (!bRunning || InBrowser != Browser || BrowserGeneration != Generation) return;
    ErrorText.Empty();
    StatusText = TEXT("Looking for local games...");
}

- (void)netServiceBrowser:(NSNetServiceBrowser*)InBrowser
    didFindService:(NSNetService*)Service moreComing:(BOOL)MoreComing
{
    if (![self isCurrentBrowser:InBrowser]) return;
    const FString Id = ServiceId(Service);
    NSString* Key = Id.GetNSString();
    {
        FScopeLock Lock(&StateMutex);
        if (!bRunning || InBrowser != Browser || BrowserGeneration != Generation ||
            Entries.Contains(Id) || Entries.Num() >= MaxResolvedServices) return;
    }
    if ([Resolving objectForKey:Key] || [Resolving count] >= MaxResolvingServices) return;
    [Resolving setObject:Service forKey:Key];
    Service.delegate = self;
    [Service scheduleInRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    [Service resolveWithTimeout:5.0];
}

- (void)netServiceBrowser:(NSNetServiceBrowser*)InBrowser
    didRemoveService:(NSNetService*)Service moreComing:(BOOL)MoreComing
{
    if (![self isCurrentBrowser:InBrowser]) return;
    const FString Id = ServiceId(Service);
    NSString* Key = Id.GetNSString();
    NSNetService* Pending = [Resolving objectForKey:Key];
    if (Pending)
    {
        Pending.delegate = nil;
        [Pending stop];
        [Pending removeFromRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
        [Resolving removeObjectForKey:Key];
    }
    {
        FScopeLock Lock(&StateMutex);
        if (!bRunning || InBrowser != Browser || BrowserGeneration != Generation) return;
        Entries.Remove(Id);
    }
    [self publishStatus];
}

- (void)netServiceBrowser:(NSNetServiceBrowser*)InBrowser
    didNotSearch:(NSDictionary<NSString*, NSNumber*>*)ErrorDictionary
{
    NSNumber* Code = [ErrorDictionary objectForKey:NSNetServicesErrorCode];
    const int32 ErrorCode = Code ? [Code intValue] : -1;
    {
        FScopeLock Lock(&StateMutex);
        if (!bRunning || InBrowser != Browser || BrowserGeneration != Generation) return;
        bRunning = false;
        Entries.Empty();
        ErrorText = FString::Printf(
            TEXT("Local game discovery failed (error %d). Allow Local Network access in Settings, then try again."),
            ErrorCode);
        StatusText = ErrorText;
    }
    [self stopOnMainThread];
}

- (void)netServiceDidResolveAddress:(NSNetService*)Service
{
    const FString Id = ServiceId(Service);
    NSString* Key = Id.GetNSString();
    if ([Resolving objectForKey:Key] != Service) return;

    FCinderLANService Entry;
    Entry.StableId = Id;
    Entry.Name = BoundedMetadata(Service.name, 64);
    if (Entry.Name.IsEmpty()) Entry.Name = TEXT("Local Cinderline game");
    Entry.Host = BoundedMetadata(Service.hostName, 253);
    Entry.Address = PreferredIPv4(Service);
    Entry.Port = static_cast<int32>(Service.port);
    Entry.bCompatible = CinderLANParseProtocol(ProtocolTXT(Service), Entry.ProtocolVersion);
    const bool bUsableEndpoint = Entry.Port > 0 && Entry.Port <= 65535 && !Entry.Endpoint().IsEmpty();

    Service.delegate = nil;
    [Service stop];
    [Service removeFromRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    [Resolving removeObjectForKey:Key];
    {
        FScopeLock Lock(&StateMutex);
        if (!bRunning || BrowserGeneration != Generation || Entries.Num() >= MaxResolvedServices) return;
        if (!bUsableEndpoint)
        {
            ErrorText = FString::Printf(TEXT("Local game %s did not provide a usable local address."), *Entry.Name);
            if (Entries.IsEmpty()) StatusText = ErrorText;
            return;
        }
        Entries.Add(Id, MoveTemp(Entry));
        ErrorText.Empty();
    }
    [self publishStatus];
}

- (void)netService:(NSNetService*)Service
    didNotResolve:(NSDictionary<NSString*, NSNumber*>*)ErrorDictionary
{
    const FString Id = ServiceId(Service);
    NSString* Key = Id.GetNSString();
    if ([Resolving objectForKey:Key] != Service) return;
    NSNumber* Code = [ErrorDictionary objectForKey:NSNetServicesErrorCode];
    const int32 ErrorCode = Code ? [Code intValue] : -1;
    const FString Name = BoundedMetadata(Service.name, 64);
    Service.delegate = nil;
    [Service stop];
    [Service removeFromRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    [Resolving removeObjectForKey:Key];
    {
        FScopeLock Lock(&StateMutex);
        if (!bRunning || BrowserGeneration != Generation) return;
        Entries.Remove(Id);
        ErrorText = FString::Printf(TEXT("Could not resolve local game %s (error %d). Try discovery again."), *Name, ErrorCode);
    }
    [self publishStatus];
}

@end

void* CinderLANAppleCreate()
{
    return [[FCinderLANBrowserBridge alloc] init];
}

void CinderLANAppleDestroy(void* Handle)
{
    FCinderLANBrowserBridge* Bridge = static_cast<FCinderLANBrowserBridge*>(Handle);
    if (!Bridge) return;
    [Bridge invalidate];
    if ([NSThread isMainThread])
    {
        [Bridge stopOnMainThread];
        [Bridge release];
        return;
    }
    FCinderLANBrowserBridge* StrongBridge = [Bridge retain];
    [Bridge release]; // Release the facade's ownership; the queued cleanup owns the bridge now.
    dispatch_async(dispatch_get_main_queue(), ^{
        [StrongBridge stopOnMainThread];
        [StrongBridge release];
    });
}

void CinderLANAppleStart(void* Handle)
{
    FCinderLANBrowserBridge* Bridge = static_cast<FCinderLANBrowserBridge*>(Handle);
    if (Bridge) [Bridge requestStart];
}

void CinderLANAppleStop(void* Handle)
{
    FCinderLANBrowserBridge* Bridge = static_cast<FCinderLANBrowserBridge*>(Handle);
    if (Bridge) [Bridge requestStop];
}

bool CinderLANAppleIsRunning(void* Handle)
{
    FCinderLANBrowserBridge* Bridge = static_cast<FCinderLANBrowserBridge*>(Handle);
    if (!Bridge) return false;
    FScopeLock Lock(&Bridge->StateMutex);
    return Bridge->bRunning;
}

TArray<FCinderLANService> CinderLANAppleServices(void* Handle)
{
    TArray<FCinderLANService> Result;
    FCinderLANBrowserBridge* Bridge = static_cast<FCinderLANBrowserBridge*>(Handle);
    if (!Bridge) return Result;
    {
        FScopeLock Lock(&Bridge->StateMutex);
        Bridge->Entries.GenerateValueArray(Result);
    }
    Result.Sort([](const FCinderLANService& A, const FCinderLANService& B)
    {
        const int32 NameOrder = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
        return NameOrder == 0 ? A.StableId.Compare(B.StableId) < 0 : NameOrder < 0;
    });
    return Result;
}

FString CinderLANAppleStatus(void* Handle)
{
    FCinderLANBrowserBridge* Bridge = static_cast<FCinderLANBrowserBridge*>(Handle);
    if (!Bridge) return FString();
    FScopeLock Lock(&Bridge->StateMutex);
    return Bridge->StatusText;
}

FString CinderLANAppleError(void* Handle)
{
    FCinderLANBrowserBridge* Bridge = static_cast<FCinderLANBrowserBridge*>(Handle);
    if (!Bridge) return FString();
    FScopeLock Lock(&Bridge->StateMutex);
    return Bridge->ErrorText;
}
